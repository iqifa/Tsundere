#pragma once

// ---------------------------------------------------------------------------
// RHIRayTracingScene — 硬件光追加速结构（BLAS / TLAS）的最小后端抽象
//
// 只服务于“在 compute shader 里用 ray query 求交”这一种用法：没有 RT pipeline、
// 没有 SBT。几何输入是 BVHBuilder 产出的“平铺”数据（BVHBuildSettings::buildTrees
// = false）：三角形按网格原始顺序排列，实例按输入顺序排列，所以
//   TLAS instanceIndex       == BVHSceneData::instances 下标
//   BLAS primitiveIndex      == 网格内三角形下标（全局 = instance.triangleOffset + primitive）
// 着色器的表面重建代码因此与软件 BVH 路径完全共用。
//
// 后端：
//   Vulkan  VK_KHR_acceleration_structure + VK_KHR_ray_query（设备支持时）
//   OpenGL  不支持（IsSupported() == false），PT 使用软件 BVH
// ---------------------------------------------------------------------------

#include "Core/Core.h"
#include "HeadLine.h"
#include <cstdint>
#include <string>

struct BVHSceneData;

struct RHIRayTracingBuildStats
{
	uint32_t blasCount = 0;
	uint32_t instanceCount = 0;
	uint32_t triangleCount = 0;
	uint64_t blasBytes = 0;
	uint64_t tlasBytes = 0;
	uint64_t scratchBytes = 0;
	float cpuMilliseconds = 0.0f;    // 记录 + 提交 + 等待完成（含上传）
	float gpuBlasMilliseconds = -1.0f;   // GPU 时间戳；-1 = 不可用
	float gpuTlasMilliseconds = -1.0f;
};

class T_API RHIRayTracingScene
{
public:
	virtual ~RHIRayTracingScene() = default;

	// 用平铺几何（重新）构建全部 BLAS 和 TLAS。阻塞到 GPU 构建完成。
	// 返回 false 时保留上一次成功的结果。
	virtual bool Build(const BVHSceneData& flatGeometry) = 0;

	virtual bool IsValid() const = 0;
	virtual const RHIRayTracingBuildStats& GetStats() const = 0;

	// 当前后端 + 设备是否能做 ray query。false 时 GetUnsupportedReason() 给出原因。
	static bool IsSupported();
	static std::string GetUnsupportedReason();

	// 不支持时返回 nullptr。
	static Ref<RHIRayTracingScene> Create();
};
