#pragma once

// ---------------------------------------------------------------------------
// BVHBuilder — 场景 → 两级 BVH → GPU
//
// 两条使用路径：
//
// 1) 增量路径（PathTraceScene）：UpdateIncremental(scene)
//      遍历实例（只读 Transform / MeshRender / 子网格状态，不拷贝顶点），以
//      (Mesh::GeometryId, Mesh::GeometryRevision) 为键缓存每个网格的 BLAS 片段：
//        * 新网格 / 拓扑变了        → 只构建该网格的 BLAS 片段
//        * 几何版本变了、拓扑没变   → refit 该片段（超出 refit 次数 / 代价比例时改为重建）
//        * 不再被引用的网格         → 释放片段
//        * 只有 refit（集合不变）     → 原地覆盖这些片段，GPU 上只写对应区段，重建 TLAS
//        * BLAS 集合有变化          → 重新拼接 BLAS / 三角形 buffer（拷贝，不重建），重建 TLAS
//        * 只有变换 / 实例增删      → 只重建 TLAS + 实例 buffer，BLAS 原样复用
//        * 只有材质下标变           → 原地修补实例 buffer 的 materialIndex，不碰任何树
//      什么时候调用由调用方决定（SceneChanges 版本号变化时），本身不做全场景哈希。
//
// 2) 整体重建（旧接口，测试 / ResourceLoader 异步流程）：
//      GatherScene(scene) 主线程 → BuildBVH() 任意线程 → UploadToGPU() 主线程。
//      Update(scene) = 实例脏标记驱动的同步一条龙。
//
// 数据布局、空场景约定见 BVH.h；GLSL 侧见 res/shaders/include/BVH.glsl。
// ---------------------------------------------------------------------------

#include "BVH.h"
#include "GLHead.h"
#include "ExternalFiles.h"
#include "Scene/Scene.h"
#include "Scene/Mesh.h"
#include "Scene/Modle.h"
#include "Panels/MeshFilePath.h"
#include "Platform/RHI/RHIBuffer.h"
#include "Core/Material.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

struct BVHIncrementalResult
{
	bool geometryChanged = false;   // BLAS 集合 / 内容变了：BLAS 与三角形 buffer 重传，TLAS 重建
	bool instancesChanged = false;  // 实例列表或变换变了：TLAS + 实例 buffer 重建
	bool materialsPatched = false;  // 只有 materialIndex 变了：实例 buffer 原地修补
	bool geometryPatched = false;   // geometryChanged 的子情况：只有 refit，几何 buffer 只覆盖被 refit 的区段
	bool uploadFailed = false;
	uint32_t blasBuilt = 0;         // 本次完整构建的 BLAS 片段数（新网格 / 拓扑变了 / refit 超限）
	uint32_t blasRefitted = 0;      // 本次 refit 的 BLAS 片段数（几何变了、拓扑没变）
	uint32_t blasReleased = 0;
	uint32_t blasReused = 0;

	bool Changed() const { return geometryChanged || instancesChanged || materialsPatched; }
};

class T_API BVHBuilder
{
public:
	BVHBuilder() = default;
	~BVHBuilder() = default;
	BVHBuilder(const BVHBuilder&) = delete;
	BVHBuilder& operator=(const BVHBuilder&) = delete;

	// ---- 增量路径（主线程）----
	// upload = false 只做 CPU 部分（测试用；GPU buffer 不变）。
	BVHIncrementalResult UpdateIncremental(const Scene& scene, bool upload = true);
	// 丢弃 BLAS 缓存，下次 UpdateIncremental 从头构建。
	void ResetIncremental();
	// 下次 UpdateIncremental 必定重建 TLAS（BLAS 不动）。切换 TLAS 构建算法时用。
	void InvalidateTLAS() { m_LastInstances.clear(); m_ForceTLAS = true; }
	// 与 GetData().blas[] 一一对应的缓存键。片段每（重新）构建一次就分配新值，
	// 硬件后端可以直接拿它做 BLAS 缓存键：键不变 = 三角形内容不变。
	const std::vector<uint64_t>& GetBLASKeys() const { return m_BLASKeys; }
	// 与 GetBLASKeys() 一一对应：最近一次 UpdateIncremental 里哪些槽位是 refit 来的、哪些网格是动态的。
	const std::vector<BVHBLASHint>& GetBLASHints() const { return m_BLASHints; }

	// ---- 整体重建：阶段 1（主线程）----
	// 遍历 Transform + MeshRender，按 Mesh 去重拷贝几何，记录每个实例的变换。
	// 模型还在异步加载的实体会被跳过（加载完成后 ResourceLoader 会通知 InstanceSet）。
	void GatherScene(const Scene& scene);
	void GatherTriangles(Ref<Scene> scene) { if (scene) GatherScene(*scene); } // 旧接口名

	// ---- 阶段 2：任意线程 ----
	void BuildBVH(unsigned int maxLeafSize = 4);

	// ---- 阶段 3：主线程 ----
	// 把最近一次 BuildBVH 的结果上传。成功返回 true；失败时保留旧 buffer。
	bool UploadToGPU();

	// 同步一条龙：IsDirty() 时依次 Gather → Build → Upload。返回是否发生了重建。
	bool Update(const Scene& scene);

	// ---- GPU 资源（上传之前为 nullptr）----
	Ref<RHIBuffer> GetTLASNodeBuffer() const { return m_TLASNodeBuffer; }
	Ref<RHIBuffer> GetInstanceBuffer() const { return m_InstanceBuffer; }
	Ref<RHIBuffer> GetBLASNodeBuffer() const { return m_BLASNodeBuffer; }
	Ref<RHIBuffer> GetTriangleBuffer() const { return m_TriangleBuffer; }
	Ref<RHIBuffer> GetTriangleAttribBuffer() const { return m_TriangleAttribBuffer; }
	// 树模式要求 5 个 buffer 都在；平铺模式（GetSettings().buildTrees == false）只有
	// Instance / Triangle / TriangleAttrib 三个。
	bool IsGPUReady() const { return m_TriangleBuffer != nullptr && m_InstanceBuffer != nullptr; }

	// 每次有 buffer 上传成功 +1。
	uint64_t GetRevision() const { return m_Revision; }
	float GetUploadMilliseconds() const { return m_UploadMilliseconds; }

	// ---- CPU 结果 ----
	// 仅主线程、且没有异步 BuildBVH 在跑时访问。
	const BVHSceneData& GetData() const { return m_Data; }
	const BVHStats& GetStats() const { return m_Data.stats; }
	unsigned int GetTriangleCount() const { return m_Data.stats.triangleCount; }
	unsigned int GetInstanceCount() const { return m_Data.stats.instanceCount; }

	BVHBuildSettings& GetSettings() { return m_Settings; }
	const BVHBuildSettings& GetSettings() const { return m_Settings; }

	// 整体重建路径的实例脏标记（增量路径不用）。
	bool IsDirty() const { return m_Dirty.load(std::memory_order_acquire); }
	void MarkDirty() { m_Dirty.store(true, std::memory_order_release); }
	void MarkClean() { m_Dirty.store(false, std::memory_order_release); }

	// Engine::Material* → 材质缓冲下标。在 Gather / UpdateIncremental 之前设置；
	// 没有映射或查不到时实例的 materialIndex 为 0。
	void SetMaterialMap(const std::unordered_map<Engine::Material*, unsigned int>* map) { m_MaterialMap = map; }

private:
	struct SceneSnapshot
	{
		std::vector<BVHMeshInput>     meshes;
		std::vector<BVHInstanceInput> instances;
	};

	struct CachedBLAS
	{
		uint64_t revision = 0;   // Mesh::GeometryRevision
		uint64_t key = 0;        // 每次构建 / refit 唯一
		uint64_t refitFrom = 0;  // 本次 UpdateIncremental 由旧键 refit 而来；否则 0
		bool dynamic = false;    // 几何改过（首次构建之后）
		bool used = false;
		BVHMeshBLAS piece;
	};

	bool UploadGeometryBuffers();   // BLAS 节点（树模式）、三角形、属性
	bool UploadGeometryPatches();   // 只写 m_PatchedSlots 的区段；不可行时退回整体上传
	bool UploadInstanceBuffers();   // TLAS 节点（树模式）、实例
	uint32_t LookupMaterial(const Component::MeshRender& meshrender, size_t meshIdx) const;

	std::mutex m_Mutex;                            // 保护 m_Snapshot / m_Data 的交接
	std::shared_ptr<const SceneSnapshot> m_Snapshot;
	BVHSceneData m_Data;
	BVHBuildSettings m_Settings;

	// 增量路径状态
	std::unordered_map<uint64_t, CachedBLAS> m_BLASCache;   // Mesh::GeometryId → 片段
	std::vector<uint64_t> m_BLASOrder;                      // 拼接顺序（GeometryId），= m_Data.blas[] 顺序
	std::vector<uint64_t> m_BLASKeys;
	std::vector<BVHBLASHint> m_BLASHints;
	std::vector<uint32_t> m_PatchedSlots;                   // 本次原地覆盖的 blas[] 下标
	std::vector<BVHInstanceInput> m_LastInstances;
	uint64_t m_NextBLASKey = 1;
	bool m_IncrementalValid = false;
	bool m_ForceTLAS = false;

	Ref<RHIBuffer> m_TLASNodeBuffer;
	Ref<RHIBuffer> m_InstanceBuffer;
	Ref<RHIBuffer> m_BLASNodeBuffer;
	Ref<RHIBuffer> m_TriangleBuffer;
	Ref<RHIBuffer> m_TriangleAttribBuffer;

	uint64_t m_Revision = 0;
	float m_UploadMilliseconds = 0.0f;

	std::atomic<bool> m_Dirty{ true };             // 初始为脏，首帧构建
	const std::unordered_map<Engine::Material*, unsigned int>* m_MaterialMap = nullptr;
};
