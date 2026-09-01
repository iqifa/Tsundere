#include "VulkanDescriptorSet.h"

#include "Platform/RenderAPIConfig.h"


#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture2D.h"
#include "Platform/Vulkan/VulkanTextureCube.h"
#include "Platform/Vulkan/VulkanStorageImage.h"
#include "Debug/Debug.h"

#include <stdexcept>
#include <vector>
#ifdef RenderAPI_Vulkan

Ref<RHIDescriptorSet> RHIDescriptorSet::Create()
{
    return CreateRef<VulkanDescriptorSet>();
}
#endif

namespace
{
    RHIVulkanContext* Context()
    {
        auto& context = RHIContext::Get();
        return context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
    }
}

bool VulkanDescriptorSet::AcceptBinding(
    uint32_t binding,
    ResourceType type)
{
    const auto existing = m_LayoutBindings.find(binding);
    if (existing != m_LayoutBindings.end())
        return existing->second == type;

    if (m_Layout != VK_NULL_HANDLE)
    {
        Error_Core("VulkanDescriptorSet: binding {} was added after layout creation", binding);
        return false;
    }

    return true;
}

VulkanDescriptorSet::VulkanDescriptorSet()
{
    auto* context = Context();
    if (!context)
        throw std::runtime_error("VulkanDescriptorSet requires an active Vulkan context");

    m_Device = context->GetDevice();
    m_Pool = context->GetImGuiDescriptorPool();
    if (m_Device == VK_NULL_HANDLE || m_Pool == VK_NULL_HANDLE)
        throw std::runtime_error("VulkanDescriptorSet requires a Vulkan device and descriptor pool");
}

VulkanDescriptorSet::~VulkanDescriptorSet()
{
    if (m_Device == VK_NULL_HANDLE)
        return;

    if (!m_DescriptorSets.empty() && m_Pool != VK_NULL_HANDLE)
    {
        vkFreeDescriptorSets(
            m_Device,
            m_Pool,
            static_cast<uint32_t>(m_DescriptorSets.size()),
            m_DescriptorSets.data());
    }
    if (m_Layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);

    m_DescriptorSets.clear();
    m_DescriptorSet = VK_NULL_HANDLE;
    m_Layout = VK_NULL_HANDLE;
}

void VulkanDescriptorSet::BindTexture(
    uint32_t binding,
    Ref<RHITexture2D> texture,
    uint32_t unit)
{
    (void)unit;
    if (!AcceptBinding(binding, ResourceType::Texture2D))
        return;
    Binding value;
    value.type = ResourceType::Texture2D;
    value.ownedTexture = std::move(texture);
    value.texture = value.ownedTexture.get();
    m_Bindings[binding] = std::move(value);
}

void VulkanDescriptorSet::BindTexture(
    uint32_t binding,
    RHITexture2D* texture,
    uint32_t unit)
{
    (void)unit;
    if (!AcceptBinding(binding, ResourceType::Texture2D))
        return;
    Binding value;
    value.type = ResourceType::Texture2D;
    value.texture = texture;
    m_Bindings[binding] = std::move(value);
}

void VulkanDescriptorSet::BindCubeMap(
    uint32_t binding,
    Ref<RHITextureCube> cubemap,
    uint32_t unit)
{
    (void)unit;
    if (!AcceptBinding(binding, ResourceType::CubeMap))
        return;
    Binding value;
    value.type = ResourceType::CubeMap;
    value.cubemap = std::move(cubemap);
    m_Bindings[binding] = std::move(value);
}

void VulkanDescriptorSet::BindStorageImage(
    uint32_t binding,
    Ref<RHIStorageImage> image,
    ImageAccess access,
    uint32_t unit)
{
    (void)unit;
    if (!AcceptBinding(binding, ResourceType::StorageImage))
        return;
    Binding value;
    value.type = ResourceType::StorageImage;
    value.storageImage = std::move(image);
    value.imageAccess = access;
    m_Bindings[binding] = std::move(value);
}

void VulkanDescriptorSet::BindUniformBuffer(
    uint32_t binding,
    Ref<RHIBuffer> buffer)
{
    if (!AcceptBinding(binding, ResourceType::UniformBuffer))
        return;
    Binding value;
    value.type = ResourceType::UniformBuffer;
    value.buffer = std::move(buffer);
    m_Bindings[binding] = std::move(value);
}

void VulkanDescriptorSet::BindStorageBuffer(
    uint32_t binding,
    Ref<RHIBuffer> buffer)
{
    if (!AcceptBinding(binding, ResourceType::StorageBuffer))
        return;
    Binding value;
    value.type = ResourceType::StorageBuffer;
    value.buffer = std::move(buffer);
    m_Bindings[binding] = std::move(value);
}

VkDescriptorType VulkanDescriptorSet::ToDescriptorType(ResourceType type) const
{
    switch (type)
    {
    case ResourceType::Texture2D:
    case ResourceType::CubeMap:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case ResourceType::StorageImage:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case ResourceType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case ResourceType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

void VulkanDescriptorSet::EnsureLayout()
{
    if (m_Layout != VK_NULL_HANDLE)
        return;

    if (m_Bindings.empty())
        return;

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(m_Bindings.size());
    for (const auto& [binding, value] : m_Bindings)
    {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding;
        layoutBinding.descriptorType = ToDescriptorType(value.type);
        layoutBinding.descriptorCount = 1;
        // Storage images may be consumed by compute or graphics shaders.
        layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
            VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(layoutBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_Layout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan descriptor set layout");

    for (const auto& [binding, value] : m_Bindings)
        m_LayoutBindings[binding] = value.type;
}

void VulkanDescriptorSet::EnsureDescriptorSet()
{
    EnsureLayout();
    if (m_Layout == VK_NULL_HANDLE)
        return;

    auto& context = RHIContext::Get();
    if (!context)
        return;

    const uint64_t frameIndex = context->GetCurrentFrame().FrameIndex;
    if (m_DescriptorSetFrame != std::numeric_limits<uint64_t>::max() &&
        m_DescriptorSetFrame != frameIndex &&
        !m_DescriptorSets.empty())
    {
        // VulkanContext waits its single in-flight fence before advancing the
        // frame index, so every set from the previous frame is now recyclable.
        vkFreeDescriptorSets(
            m_Device,
            m_Pool,
            static_cast<uint32_t>(m_DescriptorSets.size()),
            m_DescriptorSets.data());
        m_DescriptorSets.clear();
        m_DescriptorSet = VK_NULL_HANDLE;
    }

    if (m_DescriptorSet != VK_NULL_HANDLE &&
        m_DescriptorSetFrame != frameIndex)
    {
        m_DescriptorSet = VK_NULL_HANDLE;
    }

    if (m_DescriptorSet != VK_NULL_HANDLE)
        return;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_Pool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &m_Layout;

    if (vkAllocateDescriptorSets(m_Device, &allocateInfo, &descriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate Vulkan descriptor set");

    m_DescriptorSets.push_back(descriptorSet);
    m_DescriptorSet = descriptorSet;
    m_DescriptorSetFrame = frameIndex;
}

void VulkanDescriptorSet::Apply(uint32_t slot)
{
    (void)slot;

    // Descriptor contents are immutable once this set may be referenced by the
    // current command buffer. Allocate a new native set for every application;
    // the context's single in-flight fence keeps older sets safe until reuse.
    m_DescriptorSet = VK_NULL_HANDLE;
    EnsureDescriptorSet();
    if (m_DescriptorSet == VK_NULL_HANDLE)
        return;

    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet> writes;
    bufferInfos.reserve(m_Bindings.size());
    imageInfos.reserve(m_Bindings.size());
    writes.reserve(m_Bindings.size());

    for (const auto& [binding, value] : m_Bindings)
    {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_DescriptorSet;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = ToDescriptorType(value.type);

        if (value.type == ResourceType::UniformBuffer ||
            value.type == ResourceType::StorageBuffer)
        {
            auto* buffer = value.buffer
                ? dynamic_cast<VulkanBuffer*>(value.buffer.get())
                : nullptr;
            if (!buffer || buffer->GetBuffer() == VK_NULL_HANDLE)
                continue;

            bufferInfos.push_back({ buffer->GetBuffer(), 0, VK_WHOLE_SIZE });
            write.pBufferInfo = &bufferInfos.back();
        }
        else if (value.type == ResourceType::Texture2D)
        {
            auto* texture = value.texture
                ? dynamic_cast<VulkanTexture2D*>(value.texture)
                : nullptr;
            if (!texture || texture->GetImageView() == VK_NULL_HANDLE ||
                texture->GetSampler() == VK_NULL_HANDLE)
                continue;

            imageInfos.push_back({
                texture->GetSampler(),
                texture->GetImageView(),
                texture->GetLayout() == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    : texture->GetLayout() });
            write.pImageInfo = &imageInfos.back();
        }
        else if (value.type == ResourceType::CubeMap)
        {
            auto* cubemap = value.cubemap
                ? dynamic_cast<VulkanTextureCube*>(value.cubemap.get())
                : nullptr;
            if (!cubemap || cubemap->GetImageView() == VK_NULL_HANDLE ||
                cubemap->GetSampler() == VK_NULL_HANDLE)
                continue;

            imageInfos.push_back({
                cubemap->GetSampler(),
                cubemap->GetImageView(),
                cubemap->GetLayout() });
            write.pImageInfo = &imageInfos.back();
        }
        else if (value.type == ResourceType::StorageImage)
        {
            auto* image = value.storageImage
                ? dynamic_cast<VulkanStorageImage*>(value.storageImage.get())
                : nullptr;
            if (!image || image->GetImageView() == VK_NULL_HANDLE)
                continue;

            imageInfos.push_back({
                VK_NULL_HANDLE,
                image->GetImageView(),
                image->GetLayout() == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_IMAGE_LAYOUT_GENERAL
                    : image->GetLayout() });
            write.pImageInfo = &imageInfos.back();
        }
        else
        {
            continue;
        }

        writes.push_back(write);
    }

    if (!writes.empty())
        vkUpdateDescriptorSets(
            m_Device,
            static_cast<uint32_t>(writes.size()),
            writes.data(),
            0,
            nullptr);
}

void VulkanDescriptorSet::Reset()
{
    // Keep the layout and allocated set alive: pipelines may already reference
    // this layout. Resource bindings are rebuilt for the next draw.
    m_Bindings.clear();
}

VkDescriptorSetLayout VulkanDescriptorSet::GetLayout()
{
    EnsureLayout();
    return m_Layout;
}


