#pragma once

#include "Platform/RHI/RHIRayTracing.h"
#include <vulkan/vulkan.h>
#include <vector>

// ---------------------------------------------------------------------------
// VulkanRayTracingScene — VK_KHR_acceleration_structure 的 BLAS / TLAS
//
// 输入是 BVHBuilder 的平铺几何（BVHSceneData，buildTrees = false）：
//   * 每个非空网格一个 BLAS，直接以 triangles[] 为顶点数组构建：
//     vertexData = 三角形数组 + triangleOffset*48，stride 16（vec3 + pad），无索引；
//     因此 primitiveIndex 就是网格内三角形下标，和软件 BVH 共用表面重建。
//   * 多个实例引用同一网格时复用同一个 BLAS。
//   * TLAS 实例顺序 = instances[] 顺序（instanceIndex == 着色器的 u_BVHInstances 下标）。
//     实例标志 FORCE_OPAQUE | TRIANGLE_FACING_CULL_DISABLE：第一阶段只有不透明三角形。
//
// 同步：上传、BLAS、TLAS 在一个一次性命令缓冲里录制，BLAS→TLAS 之间有
// ACCELERATION_STRUCTURE 读写屏障；提交后 vkQueueWaitIdle，因此帧命令缓冲里的
// compute 读取一定看到完整的加速结构（与 VulkanBuffer::Upload 同一模式）。
// 静态几何只在场景变化时构建一次，这次阻塞是可接受的；旧的 AS / buffer
// 走 VulkanDeletionQueue，等在途帧结束后再释放。
//
// 不做 update / compaction：BLAS 不带 ALLOW_UPDATE，实例或几何变化一律整体重建。
// ---------------------------------------------------------------------------
class T_API VulkanRayTracingScene final : public RHIRayTracingScene
{
public:
	VulkanRayTracingScene();
	~VulkanRayTracingScene() override;

	bool Build(const BVHSceneData& flatGeometry) override;
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
	void ReleaseCurrent();

	VkDevice m_Device = VK_NULL_HANDLE;
	VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
	VkQueue m_Queue = VK_NULL_HANDLE;
	uint32_t m_QueueFamily = 0;

	std::vector<VkAccelerationStructureKHR> m_BLAS;
	VkAccelerationStructureKHR m_TLAS = VK_NULL_HANDLE;
	DeviceBuffer m_BLASStorage;
	DeviceBuffer m_TLASStorage;

	RHIRayTracingBuildStats m_Stats;
};
