#pragma once

// ---------------------------------------------------------------------------
// PathTraceScene — GPU Path Tracing 的 CPU 侧场景数据（两后端共用）
//
// 每帧在主线程调用一次 Update(scene)，由 SceneChanges 的分类版本号驱动（见 SceneChanges.h），
// 静态场景不做任何扫描：
//   MeshGeometry        → 该 Mesh 的 BLAS 片段 + 拼接 + TLAS
//   InstanceTransform   → 实例数据 + TLAS（BLAS 复用）
//   InstanceSet         → 实例列表 + TLAS，按需建 / 删 BLAS；材质表
//   MaterialAssignment  → 材质表 + 实例 materialIndex 原地修补（不重建空间结构）
//   MaterialParameters  → 只重传材质 buffer
//   Lights              → 只重传灯光 buffer
//   SoftwareBVH：BVHBuilder 树模式（5 个 SSBO）
//   RayQuery   ：BVHBuilder 平铺模式（3 个 SSBO）+ RHIRayTracingScene 增量 BLAS / TLAS
// 版本号变了但派生数据比较后没有差异（例如改了 PT 不用的参数）时不重置累积。
// 返回值表示“这一帧积分结果会变”，PathTracePass 据此清空累积。
// 开发构建（ENABLE_PT_DIRTY_VALIDATION）会在消费版本号之前做漏标校验，见 PTDirtyValidation.h。
// 逐射线 / 逐反弹没有任何 CPU 参与，也没有任何读回。
//
// GPU 布局的 GLSL 镜像在 res/shaders/include/PathTraceCommon.glsl。
// ---------------------------------------------------------------------------

#include "Scene/BVHBuilder.h"
#include "Platform/RHI/RHIRayTracing.h"
#include "Pipeline/PathTracing/PTDirtyValidation.h"
#include "Scene/SceneChanges.h"
#include <glm/glm.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Scene;

namespace PathTracing
{
	enum class Backend : uint32_t
	{
		SoftwareBVH = 0,   // 自定义 BVH + compute 软件遍历（OpenGL；Vulkan 的回退）
		RayQuery = 1       // Vulkan VK_KHR_ray_query
	};

	constexpr uint32_t MaterialDiffuse = 0;
	constexpr uint32_t MaterialMetal = 1;

	// std430，80 字节。PathTraceCommon.glsl: PTMaterial
	struct GPUMaterial
	{
		glm::vec4 baseColor{ 0.8f, 0.8f, 0.8f, 1.0f };
		glm::vec4 emission{ 0.0f };
		float roughness = 0.5f;
		float metallic = 0.0f;
		uint32_t type = MaterialDiffuse;
		uint32_t flags = 0;
		glm::vec4 tilingOffset{ 1, 1, 0, 0 };
		glm::uvec4 baseColorTexture{ 0u }; // padded texel offset, width, height, sampling flags
	};
	static_assert(sizeof(GPUMaterial) == 80 && offsetof(GPUMaterial, roughness) == 32 &&
		offsetof(GPUMaterial, baseColorTexture) == 64,
		"GPUMaterial must match PTMaterial in PathTraceCommon.glsl");

	// std430，64 字节。与 DeferredLightPass::GPULight / Lighting.glsl Light 同布局。
	struct GPULight
	{
		glm::vec4 directionAmbient{ 0.0f };   // xyz = 方向（光的传播方向）, w = ambient（PT 不用）
		glm::vec4 colorIntensity{ 0.0f };     // rgb = 颜色, a = 强度
		glm::vec4 positionType{ 0.0f };       // xyz = 位置, w = 类型
		glm::vec4 params{ 0.0f };             // 点/聚光: 半径, 衰减, 内角, 外角；面光: 宽, 高, 形状
	};
	static_assert(sizeof(GPULight) == 64, "GPULight must match Light in Lighting.glsl");

	// std140，208 字节。PathTraceCommon.glsl: PerPass_PathTrace
	struct FrameUBO
	{
		glm::mat4 invView{ 1.0f };
		glm::mat4 invProj{ 1.0f };
		glm::vec4 cameraPos{ 0.0f };
		glm::uvec4 frame{ 0u };      // 已累积样本, 本次样本数, 最大反弹, 帧种子
		glm::uvec4 config{ 0u };     // 灯光数, 调试视图, 宽, 高
		glm::vec4 environment{ 0.0f };
		glm::vec4 params{ 0.0f };    // 亮度上限, 轮盘起始反弹, 调试深度范围
	};
	static_assert(sizeof(FrameUBO) == 208 && offsetof(FrameUBO, frame) == 144 &&
		offsetof(FrameUBO, params) == 192, "FrameUBO must match PerPass_PathTrace (std140)");

	// 着色器里的软件遍历栈容量（res/shaders/include/BVH.glsl）。
	constexpr uint32_t ShaderTLASStack = 24;
	constexpr uint32_t ShaderBLASStack = 32;

	// 材质参数 → GPUMaterial。认 GBuffer / MaterialTest 的 u_BaseColor 等参数名，
	// 再退回旧着色器常用名。金属度连续混合漫反射与 GGX，与前向/延迟相同。
	T_API GPUMaterial ExtractMaterial(const Engine::Material* material);
}

class T_API PathTraceScene
{
public:
	// 最近一次“有工作”的 Update 做了什么（UI / 调试用）
	struct UpdateInfo
	{
		uint32_t dirtyMask = 0;            // 本次消费的 SceneChanges 类别
		uint32_t missedMask = 0;           // 开发校验发现的漏标类别（已自愈）
		BVHIncrementalResult geometry;
		bool accelUpdated = false;         // 硬件 BLAS/TLAS 更新过
		bool materialsUploaded = false;
		bool lightsUploaded = false;
		bool resetAccumulation = false;
		float cpuMilliseconds = 0.0f;
	};

	explicit PathTraceScene(PathTracing::Backend backend);
	~PathTraceScene();
	PathTraceScene(const PathTraceScene&) = delete;
	PathTraceScene& operator=(const PathTraceScene&) = delete;

	// 主线程，每帧一次。返回 true = 影响积分结果的数据变了，需要重置累积。
	// 只看 SceneChanges 版本号：没有任何类别变化时直接返回，不扫描场景。
	bool Update(const Scene& scene);

	PathTracing::Backend GetBackend() const { return m_Backend; }
	// 硬件加速结构构建失败（显存不足等），调用方应换成 SoftwareBVH。
	bool HasFailed() const { return m_Failed; }
	bool IsReady() const;

	BVHBuilder& GetGeometry() { return m_Geometry; }
	const BVHBuilder& GetGeometry() const { return m_Geometry; }
	Ref<RHIRayTracingScene> GetRayTracingScene() const { return m_RayTracing; }
	Ref<RHIBuffer> GetMaterialBuffer() const { return m_MaterialBuffer; }
	Ref<RHIBuffer> GetLightBuffer() const { return m_LightBuffer; }
	Ref<RHIBuffer> GetBaseColorBuffer() const { return m_BaseColorBuffer; }
	struct BaseColorSource
	{
		Ref<RHITexture2D> texture;
		glm::uvec4 layout{ 0u }; // offset, padded width/height, decode flags
	};
	const std::vector<BaseColorSource>& GetBaseColorSources() const { return m_BaseColorSources; }
	uint32_t GetLightCount() const { return static_cast<uint32_t>(m_Lights.size()); }
	uint32_t GetMaterialCount() const { return static_cast<uint32_t>(m_Materials.size()); }

	// 开发期漏标校验（仅 ENABLE_PT_DIRTY_VALIDATION 构建生效）。interval = 每几帧检查一次。
	static constexpr bool ValidationCompiled = ENABLE_PT_DIRTY_VALIDATION != 0;
	void SetValidation(bool enabled, uint32_t interval);

	// 软件 BVH 的 TLAS 构建算法（LBVH / SAH）。变化时下一次 Update 只重建 TLAS。
	// 硬件 Ray Query 没有自建的树，设置无效。
	void SetTLASBuilder(BVHTLASBuilder builder);
	BVHTLASBuilder GetTLASBuilder() const { return m_Geometry.GetSettings().tlasBuilder; }
	// BLAS refit 策略（两个后端都生效；硬件后端用 UPDATE 模式）。只影响之后的几何变化，不触发重建。
	void SetBLASRefit(bool enabled, uint32_t maxRefits, float costRatio)
	{
		BVHBuildSettings& settings = m_Geometry.GetSettings();
		settings.blasRefit = enabled;
		settings.maxBLASRefits = maxRefits;
		settings.blasRefitCostRatio = costRatio;
	}
	uint64_t GetMissedNotificationTotal() const;
	const std::string& GetLastValidationReport() const;

	// 统计（UI 显示）
	uint64_t GetGeometryRevision() const { return m_GeometryRevision; }
	float GetLastGatherMilliseconds() const { return m_LastUpdate.cpuMilliseconds; }
	const UpdateInfo& GetLastUpdate() const { return m_LastUpdate; }
	const std::string& GetStatus() const { return m_Status; }

private:
	bool RebuildMaterialTable(const Scene& scene);   // true = 表内容（指针序列）变了
	bool UpdateGeometry(const Scene& scene, UpdateInfo& info);
	bool UpdateMaterials();
	bool UpdateLights(const Scene& scene);
	void RefreshStatus();

	PathTracing::Backend m_Backend;
	BVHBuilder m_Geometry;
	Ref<RHIRayTracingScene> m_RayTracing;
	bool m_Failed = false;

	// 已处理到的 SceneChanges 版本（本消费方私有，不影响其他消费方）
	SceneChanges::Versions m_Seen;
	bool m_HasSeen = false;
	const Scene* m_LastScene = nullptr;
	uint64_t m_LastSceneRevision = 0;

	// 材质表：0 号是默认材质（没有分配材质的子网格）
	std::unordered_map<Engine::Material*, unsigned int> m_MaterialIndex;
	std::vector<Ref<Engine::Material>> m_MaterialRefs;   // 保活：读取参数期间指针有效
	std::vector<PathTracing::GPUMaterial> m_Materials;
	std::vector<PathTracing::GPULight> m_Lights;
	Ref<RHIBuffer> m_MaterialBuffer;
	Ref<RHIBuffer> m_LightBuffer;
	Ref<RHIBuffer> m_BaseColorBuffer;
	std::vector<BaseColorSource> m_BaseColorSources;
	uint32_t m_MaterialCapacity = 0;
	uint32_t m_LightCapacity = 0;

	std::unique_ptr<PTDirtyValidator> m_Validator;
	bool m_ValidationEnabled = ENABLE_PT_DIRTY_VALIDATION != 0;
	uint32_t m_ValidationInterval = 1;
	uint32_t m_ValidationCountdown = 0;
	bool m_ForceTLAS = false;   // TLAS 构建算法换了：即使场景没变也要重建

	uint64_t m_GeometryRevision = 0;
	UpdateInfo m_LastUpdate;
	std::string m_Status;
};
