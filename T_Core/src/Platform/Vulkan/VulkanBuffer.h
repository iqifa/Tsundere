#pragma once

#include "Platform/RHI/RHIBuffer.h"
#include "Platform/Vulkan/VulkanMemoryPool.h"
#include <vulkan/vulkan.h>

class T_API VulkanBuffer : public RHIBuffer
{
public:
    explicit VulkanBuffer(const BufferDesc& desc);
    ~VulkanBuffer() override;

    void Upload(const void* data, uint32_t size, uint32_t offset = 0) override;
    // RHIBuffer::UploadBatch 的实现：设备本地区段一次提交、一次等待。
    static void UploadRegions(const std::vector<BufferUploadRegion>& regions);
    void* Map() override;
    void Unmap() override;
    uint32_t GetSize() const override { return m_Size; }
    uintptr_t GetNativeID() const override
    {
        return reinterpret_cast<uintptr_t>(m_Buffer);
    }

    VkBuffer GetBuffer() const { return m_Buffer; }
    VkDeviceMemory GetMemory() const { return m_Allocation.memory; }
    VkDeviceSize GetMemoryOffset() const { return m_Allocation.offset; }
    bool IsHostVisible() const { return m_HostVisible; }

private:
    void Create(const BufferDesc& desc);
    void Destroy();

    VkDevice m_Device = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkQueue m_Queue = VK_NULL_HANDLE;
    uint32_t m_QueueFamily = 0;
    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VulkanMemoryAllocation m_Allocation;
    uint32_t m_Size = 0;
    BufferUsage m_Usage = BufferUsage::Vertex;
    bool m_HostVisible = false;
    void* m_Mapped = nullptr;
};
