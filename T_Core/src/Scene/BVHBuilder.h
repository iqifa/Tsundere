#pragma once

// ---------------------------------------------------------------------------
// BVHBuilder — 场景 → 两级 BVH → GPU
//
// 三个阶段，对应 ResourceLoader 的异步流程：
//   1. GatherScene(scene)   主线程  拷贝一份几何 + 实例快照（之后不再访问 Scene）
//   2. BuildBVH()           任意线程 纯 CPU 构建（BLAS × N + TLAS）
//   3. UploadToGPU()        主线程  创建 5 个 SSBO
// 同步使用直接调 Update(scene)：脏了才做 1+2+3。
//
// 数据布局、空场景约定见 BVH.h；GLSL 侧见 res/shaders/include/BVH.glsl。
// 目前是整体重建（脏标记驱动），还没有增量 / 动态更新。
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

class T_API BVHBuilder
{
public:
	BVHBuilder() = default;
	~BVHBuilder();
	BVHBuilder(const BVHBuilder&) = delete;
	BVHBuilder& operator=(const BVHBuilder&) = delete;

	// 全局脏标记：场景几何 / 变换 / 模型加载发生变化时从任何地方调用。
	static void MarkActiveDirty()
	{
		if (s_ActiveInstance)
			s_ActiveInstance->MarkDirty();
	}
	static void SetActiveInstance(BVHBuilder* builder) { s_ActiveInstance = builder; }
	static BVHBuilder* GetActiveInstance() { return s_ActiveInstance; }

	// ---- 阶段 1：主线程 ----
	// 遍历 Transform + MeshRender，按 Mesh 去重拷贝几何，记录每个实例的变换。
	// 模型还在异步加载的实体会被跳过（加载完成后 ResourceLoader 会再次标脏）。
	void GatherScene(const Scene& scene);
	void GatherTriangles(Ref<Scene> scene) { if (scene) GatherScene(*scene); } // 旧接口名

	// ---- 阶段 2：任意线程 ----
	// 对最近一次 Gather 的快照做构建。构建期间可以再次 Gather，互不影响。
	void BuildBVH(unsigned int maxLeafSize = 4);

	// ---- 阶段 3：主线程 ----
	// 把最近一次 BuildBVH 的结果上传。成功返回 true；失败时保留旧 buffer。
	bool UploadToGPU();

	// 同步一条龙：IsDirty() 时依次 Gather → Build → Upload。返回是否发生了重建。
	bool Update(const Scene& scene);

	// ---- GPU 资源（UploadToGPU 之前为 nullptr）----
	Ref<RHIBuffer> GetTLASNodeBuffer() const { return m_TLASNodeBuffer; }
	Ref<RHIBuffer> GetInstanceBuffer() const { return m_InstanceBuffer; }
	Ref<RHIBuffer> GetBLASNodeBuffer() const { return m_BLASNodeBuffer; }
	Ref<RHIBuffer> GetTriangleBuffer() const { return m_TriangleBuffer; }
	Ref<RHIBuffer> GetTriangleAttribBuffer() const { return m_TriangleAttribBuffer; }
	bool IsGPUReady() const { return m_TLASNodeBuffer != nullptr; }

	// ---- CPU 结果 ----
	// 仅主线程、且没有异步 BuildBVH 在跑时访问。
	const BVHSceneData& GetData() const { return m_Data; }
	const BVHStats& GetStats() const { return m_Data.stats; }
	unsigned int GetTriangleCount() const { return m_Data.stats.triangleCount; }
	unsigned int GetInstanceCount() const { return m_Data.stats.instanceCount; }

	BVHBuildSettings& GetSettings() { return m_Settings; }

	// 脏标记：一帧延迟的旧 BVH 可以接受，避免每帧 O(N log N) 重建。
	bool IsDirty() const { return m_Dirty.load(std::memory_order_acquire); }
	void MarkDirty() { m_Dirty.store(true, std::memory_order_release); }
	void MarkClean() { m_Dirty.store(false, std::memory_order_release); }

	// Engine::Material* → 材质缓冲下标。在 GatherScene 之前设置；
	// 没有映射或查不到时实例的 materialIndex 为 0。
	void SetMaterialMap(const std::unordered_map<Engine::Material*, unsigned int>* map) { m_MaterialMap = map; }

private:
	struct SceneSnapshot
	{
		std::vector<BVHMeshInput>     meshes;
		std::vector<BVHInstanceInput> instances;
	};

	std::mutex m_Mutex;                            // 保护 m_Snapshot / m_Data 的交接
	std::shared_ptr<const SceneSnapshot> m_Snapshot;
	BVHSceneData m_Data;
	BVHBuildSettings m_Settings;

	Ref<RHIBuffer> m_TLASNodeBuffer;
	Ref<RHIBuffer> m_InstanceBuffer;
	Ref<RHIBuffer> m_BLASNodeBuffer;
	Ref<RHIBuffer> m_TriangleBuffer;
	Ref<RHIBuffer> m_TriangleAttribBuffer;

	std::atomic<bool> m_Dirty{ true };             // 初始为脏，首帧构建
	const std::unordered_map<Engine::Material*, unsigned int>* m_MaterialMap = nullptr;

	static BVHBuilder* s_ActiveInstance;
};
