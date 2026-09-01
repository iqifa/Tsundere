#pragma once

#include "Platform/RHI/RHIDescriptorSet.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

class T_API VulkanDescriptorSet : public RHIDescriptorSet
{
public:
    VulkanDescriptorSet();
    ~VulkanDescriptorSet() override;

    void BindTexture(uint32_t binding, Ref<RHITexture2D> texture, uint32_t unit) override;
    void BindTexture(uint32_t binding, RHITexture2D* texture, uint32_t unit) override;
    void BindCubeMap(uint32_t binding, Ref<RHITextureCube> cubemap, uint32_t unit) override;
    void BindStorageImage(uint32_t binding, Ref<RHIStorageImage> image, ImageAccess access, uint32_t unit) override;
    void BindUniformBuffer(uint32_t binding, Ref<RHIBuffer> buffer) override;
    void BindStorageBuffer(uint32_t binding, Ref<RHIBuffer> buffer) override;

    void Apply(uint32_t slot = 0) override;
    void Reset() override;

    VkDescriptorSetLayout GetLayout();
    uintptr_t GetLayoutNativeID() const override
    {
        return reinterpret_cast<uintptr_t>(m_Layout);
    }
    VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }

private:
    enum class ResourceType
    {
        Texture2D,
        CubeMap,
        StorageImage,
        UniformBuffer,
        StorageBuffer
    };

    struct Binding
    {
        ResourceType type = ResourceType::UniformBuffer;
        Ref<RHITexture2D> ownedTexture;
        RHITexture2D* texture = nullptr;
        Ref<RHITextureCube> cubemap;
        Ref<RHIStorageImage> storageImage;
        Ref<RHIBuffer> buffer;
        ImageAccess imageAccess = ImageAccess::ReadWrite;
    };

    bool AcceptBinding(uint32_t binding, ResourceType type);
    void EnsureLayout();
    void EnsureDescriptorSet();
    VkDescriptorType ToDescriptorType(ResourceType type) const;

    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
    VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_DescriptorSets;
    uint64_t m_DescriptorSetFrame = std::numeric_limits<uint64_t>::max();
    std::map<uint32_t, Binding> m_Bindings;
    std::map<uint32_t, ResourceType> m_LayoutBindings;
};
