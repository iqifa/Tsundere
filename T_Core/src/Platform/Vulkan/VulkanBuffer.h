#pragma once

#include "Platform/RHI/RHIBuffer.h"
#include <vulkan/vulkan.h>

class T_API VulkanBuffer : public RHIBuffer
{
public:
    explicit VulkanBuffer(const BufferDesc& desc);
    ~VulkanBuffer() override;

    void Upload(const void* data, uint32_t size, uint32_t offset = 0) override;
    void* Map() override;
    void Unmap() override;
    uint32_t GetSize() const override { return m_Size; }
    uintptr_t GetNativeID() const override
    {
        return reinterpret_cast<uintptr_t>(m_Buffer);
    }

    VkBuffer GetBuffer() const { return m_Buffer; }
    VkDeviceMemory GetMemory() const { return m_Memory; }
    bool IsHostVisible() const { return m_HostVisible; }

private:
    void Create(const BufferDesc& desc);
    void Destroy();
    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

    VkDevice m_Device = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkQueue m_Queue = VK_NULL_HANDLE;
    uint32_t m_QueueFamily = 0;
    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    uint32_t m_Size = 0;
    BufferUsage m_Usage = BufferUsage::Vertex;
    bool m_HostVisible = false;
    void* m_Mapped = nullptr;
};
