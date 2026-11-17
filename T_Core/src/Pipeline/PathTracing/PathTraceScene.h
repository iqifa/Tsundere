#pragma once

// ---------------------------------------------------------------------------
// PathTraceScene — GPU Path Tracing 的 CPU 侧场景数据（两后端共用）
//
// 每帧在主线程调用一次 Update(scene)：
//   * 几何 / 实例 / 材质分配变化（哈希 + BVHBuilder 脏标记）→ 重新采集并上传
//       SoftwareBVH：BVHBuilder 树模式（5 个 SSBO）
//       RayQuery   ：BVHBuilder 平铺模式（3 个 SSBO）+ RHIRayTracingScene 构建 BLAS/TLAS
//   * 材质参数变化 → 只重传材质 buffer
//   * 灯光变化     → 只重传灯光 buffer
// 返回值表示“这一帧积分结果会变”，PathTracePass 据此清空累积。
// 逐射线 / 逐反弹没有任何 CPU 参与，也没有任何读回。
//
// GPU 布局的 GLSL 镜像在 res/shaders/include/PathTraceCommon.glsl。
// ---------------------------------------------------------------------------

#include "Scene/BVHBuilder.h"
#include "Platform/RHI/RHIRayTracing.h"
#include <glm/glm.hpp>
#include <cstddef>
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

	// std430，48 字节。PathTraceCommon.glsl: PTMaterial
	struct GPUMaterial
	{
		glm::vec4 baseColor{ 0.8f, 0.8f, 0.8f, 1.0f };
		glm::vec4 emission{ 0.0f };
		float roughness = 0.5f;
		float metallic = 0.0f;
		uint32_t type = MaterialDiffuse;
		uint32_t flags = 0;
	};
	static_assert(sizeof(GPUMaterial) == 48 && offsetof(GPUMaterial, roughness) == 32,
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
	// 再退回旧着色器常用名。metallic >= 0.5 视为金属（GGX），否则漫反射。
	T_API GPUMaterial ExtractMaterial(const Engine::Material* material);
}

class T_API PathTraceScene
{
public:
	explicit PathTraceScene(PathTracing::Backend backend);
	~PathTraceScene();
	PathTraceScene(const PathTraceScene&) = delete;
	PathTraceScene& operator=(const PathTraceScene&) = delete;

	// 主线程，每帧一次。返回 true = 影响积分结果的数据变了，需要重置累积。
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
	uint32_t GetLightCount() const { return static_cast<uint32_t>(m_Lights.size()); }
	uint32_t GetMaterialCount() const { return static_cast<uint32_t>(m_Materials.size()); }

	// 统计（UI 显示）
	uint64_t GetGeometryRevision() const { return m_GeometryRevision; }
	float GetLastGatherMilliseconds() const { return m_GatherMs; }
	const std::string& GetStatus() const { return m_Status; }

private:
	void RebuildGeometry(const Scene& scene);
	bool UpdateMaterials();
	bool UpdateLights(const Scene& scene);
	uint64_t HashGeometry(const Scene& scene) const;
	uint64_t HashMaterialParameters() const;

	PathTracing::Backend m_Backend;
	BVHBuilder m_Geometry;
	Ref<RHIRayTracingScene> m_RayTracing;
	bool m_Failed = false;
	bool m_HasGeometry = false;

	// 材质表：0 号是默认材质（没有分配材质的子网格）
	std::unordered_map<Engine::Material*, unsigned int> m_MaterialIndex;
	std::vector<Ref<Engine::Material>> m_MaterialRefs;   // 保活：哈希/读取参数期间指针有效
	std::vector<PathTracing::GPUMaterial> m_Materials;
	std::vector<PathTracing::GPULight> m_Lights;
	Ref<RHIBuffer> m_MaterialBuffer;
	Ref<RHIBuffer> m_LightBuffer;
	uint32_t m_MaterialCapacity = 0;
	uint32_t m_LightCapacity = 0;

	uint64_t m_GeometryHash = 0;
	uint64_t m_MaterialHash = 0;
	uint64_t m_GeometryRevision = 0;
	float m_GatherMs = 0.0f;
	std::string m_Status;
};
