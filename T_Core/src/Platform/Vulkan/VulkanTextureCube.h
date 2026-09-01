#pragma once

#include "Platform/RHI/RHITexture.h"
#include <vulkan/vulkan.h>

class T_API VulkanTextureCube : public RHITextureCube
{
public:
    explicit VulkanTextureCube(const TextureCubeDesc& desc);
    ~VulkanTextureCube() override;

    void Bind(uint32_t slot) override;
    uintptr_t GetNativeID() const override
    {
        return reinterpret_cast<uintptr_t>(m_ImageView);
    }

    VkImage GetImage() const { return m_Image; }
    VkImageView GetImageView() const { return m_ImageView; }
    VkSampler GetSampler() const { return m_Sampler; }
    VkImageLayout GetLayout() const { return m_Layout; }
    uint32_t GetSize() const { return m_Size; }

private:
    void Create(const TextureCubeDesc& desc);
    void Upload(const std::vector<unsigned char>& pixels, uint32_t faceSize);
    void Destroy();
    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

    VkDevice m_Device = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkQueue m_Queue = VK_NULL_HANDLE;
    uint32_t m_QueueFamily = 0;
    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkImageLayout m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t m_Size = 0;
};
