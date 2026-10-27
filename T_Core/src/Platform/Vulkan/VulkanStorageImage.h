#pragma once

#include "Platform/RHI/RHITexture.h"
#include <vulkan/vulkan.h>
#include <string>

// Vulkan implementation of RHIStorageImage. A storage image is represented by
// an image view and is exposed to shaders through VK_DESCRIPTOR_TYPE_STORAGE_IMAGE.
class T_API VulkanStorageImage : public RHIStorageImage
{
public:
    explicit VulkanStorageImage(const StorageImageDesc& desc);
    ~VulkanStorageImage() override;

    void BindAsImage(uint32_t slot, ImageAccess access) override;
    void UnbindAsImage(uint32_t slot) override;
    void BindAsTexture(uint32_t slot) override;
    void Resize(uint32_t w, uint32_t h) override;

    uint32_t GetWidth() const override { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    uintptr_t GetNativeID() const override
    {
        return reinterpret_cast<uintptr_t>(m_ImageView);
    }

    VkImage GetImage() const { return m_Image; }
    VkImageView GetImageView() const { return m_ImageView; }
    VkFormat GetVkFormat() const { return m_Format; }
    VkImageLayout GetLayout() const { return m_Layout; }
    void SetLayout(VkImageLayout layout) { m_Layout = layout; }

private:
    void Create(const StorageImageDesc& desc);
    void Destroy();
    void TransitionToGeneral();
    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

    VkDevice m_Device = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    VkImageLayout m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
};