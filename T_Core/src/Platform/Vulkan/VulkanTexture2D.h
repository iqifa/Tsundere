#pragma once

#include "Platform/RHI/RHITexture.h"
#include <vulkan/vulkan.h>

class T_API VulkanTexture2D : public RHITexture2D
{
public:
    explicit VulkanTexture2D(const Texture2DDesc& desc);
    ~VulkanTexture2D() override;

    void Bind(uint32_t slot) override;
    void Unbind() override;
    void BindAsImage(uint32_t slot, ImageAccess access) override;
    void UnbindAsImage(uint32_t slot) override;
    void Resize(uint32_t w, uint32_t h) override;

    uint32_t GetWidth() const override { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    Format GetFormat() const override { return m_Format; }
    uint32_t GetSampleCount() const override { return m_SampleCount; }
    uintptr_t GetNativeID() const override
    {
        return reinterpret_cast<uintptr_t>(m_ImageView);
    }
    const std::string& GetPath() const override { return m_FilePath; }

    VkImage GetImage() const { return m_Image; }
    VkImageView GetImageView() const { return m_ImageView; }
    VkSampler GetSampler() const { return m_Sampler; }
    VkImageAspectFlags GetAspectMask() const { return m_AspectMask; }
    VkImageLayout GetLayout() const { return m_Layout; }
    void SetLayout(VkImageLayout layout) { m_Layout = layout; }

private:
    void Create(const Texture2DDesc& desc);
    void Destroy();
    void UploadPixels(const Texture2DDesc& desc, VkFormat format);
    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

    VkDevice m_Device = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkQueue m_Queue = VK_NULL_HANDLE;
    uint32_t m_QueueFamily = 0;
    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkImageAspectFlags m_AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_SampleCount = 1;
    Format m_Format = Format::Unknown;
    TextureUsage m_Usage = TextureUsage::Sampled;
    std::string m_FilePath;
};
