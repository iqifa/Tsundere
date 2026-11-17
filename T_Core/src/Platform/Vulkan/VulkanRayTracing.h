#pragma once

#include "Platform/RHI/RHIRayTracing.h"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// VulkanRayTracingScene — VK_KHR_acceleration_structure 的 BLAS / TLAS（增量）
//
// 输入是 BVHBuilder 的平铺几何（BVHSceneData，buildTrees = false）+ 每个网格的 BLAS 键：
//   * 每个非空网格一个 BLAS，以该网格的平铺三角形为顶点数组构建（stride 16，无索引），
//     因此 primitiveIndex 就是网格内三角形下标，和软件 BVH 共用表面重建。
//   * BLAS 按键缓存、各自独立存储：键不变就复用，只构建新键，不再引用的键延迟释放。
//     物体移动 / 实例增删只重建 TLAS；材质变化完全不经过这里。
//   * TLAS 实例顺序 = instances[] 顺序（instanceIndex == 着色器的 u_BVHInstances 下标）。
//     实例标志 FORCE_OPAQUE | TRIANGLE_FACING_CULL_DISABLE：第一阶段只有不透明三角形。
//
// 同步：上传、BLAS、TLAS 在一个一次性命令缓冲里录制，BLAS→TLAS 之间有
// ACCELERATION_STRUCTURE 读写屏障；提交后 vkQueueWaitIdle，因此帧命令缓冲里的
// compute 读取一定看到完整的加速结构（与 VulkanBuffer::Upload 同一模式）。
// 旧的 AS / buffer 走 VulkanDeletionQueue，等在途帧结束后再释放。
//
// 不做 refit / compaction：BLAS 不带 ALLOW_UPDATE，几何变化即换键重建。
// ---------------------------------------------------------------------------
class T_API VulkanRayTracingScene final : public RHIRayTracingScene
{
public:
	VulkanRayTracingScene();
	~VulkanRayTracingScene() override;

	bool Update(const BVHSceneData& flatGeometry, const std::vector<uint64_t>& blasKeys) override;
	void Clear() override;
	bool IsValid() const override { return m_TLAS != VK_NULL_HANDLE; }
	const RHIRayTracingBuildStats& GetStats() const override { return m_Stats; }

	VkAccelerationStructureKHR GetTLAS() const { return m_TLAS; }

	struct DeviceBuffer
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDeviceAddress address = 0;
		VkDeviceSize size = 0;
		void* mapped = nullptr;
	};

private:
	struct CachedBLAS
	{
		VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
		DeviceBuffer storage;
		VkDeviceAddress address = 0;
		VkDeviceSize bytes = 0;
	};

	// 延迟销毁：AS 对象先、存储 buffer 后。
	void Retire(std::vector<VkAccelerationStructureKHR> handles, std::vector<DeviceBuffer> storage);

	VkDevice m_Device = VK_NULL_HANDLE;
	VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
	VkQueue m_Queue = VK_NULL_HANDLE;
	uint32_t m_QueueFamily = 0;

	std::unordered_map<uint64_t, CachedBLAS> m_BLASCache;   // BVHBuilder BLAS 键 → BLAS
	VkAccelerationStructureKHR m_TLAS = VK_NULL_HANDLE;
	DeviceBuffer m_TLASStorage;

	RHIRayTracingBuildStats m_Stats;
};
