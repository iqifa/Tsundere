#include "VulkanStorageImage.h"

#include "Platform/RenderAPIConfig.h"
#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHIVulkanContext.h"

#include <stdexcept>

#ifdef RenderAPI_Vulkan

Ref<RHIStorageImage> RHIStorageImage::Create(const StorageImageDesc& desc)
{
    return CreateRef<VulkanStorageImage>(desc);
}

namespace
{
    VkFormat ToVkStorageFormat(Format format)
    {
        switch (format)
        {
        case Format::R8_UNORM:   return VK_FORMAT_R8_UNORM;
        case Format::R16F:       return VK_FORMAT_R16_SFLOAT;
        case Format::RG16F:      return VK_FORMAT_R16G16_SFLOAT;
        case Format::RGBA16F:    return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::RGBA32F:    return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::R32_UINT:   return VK_FORMAT_R32_UINT;
        case Format::R32_SINT:   return VK_FORMAT_R32_SINT;
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }
}

VulkanStorageImage::VulkanStorageImage(const StorageImageDesc& desc)
{
    Create(desc);
}

VulkanStorageImage::~VulkanStorageImage()
{
    Destroy();
}

void VulkanStorageImage::Create(const StorageImageDesc& desc)
{
    auto& context = RHIContext::Get();
    auto* vkContext = context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
    if (!vkContext)
        throw std::runtime_error("VulkanStorageImage requires an active Vulkan context");

    m_Device = vkContext->GetDevice();
    m_PhysicalDevice = vkContext->GetPhysicalDevice();
    m_Width = desc.width;
    m_Height = desc.height;
    m_Format = ToVkStorageFormat(desc.format);
    if (m_Device == VK_NULL_HANDLE || m_PhysicalDevice == VK_NULL_HANDLE ||
        m_Width == 0 || m_Height == 0 || m_Format == VK_FORMAT_UNDEFINED)
    {
        throw std::runtime_error("Invalid Vulkan storage image description");
    }

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, m_Format, &formatProperties);
    if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
        throw std::runtime_error("Vulkan format does not support storage images");

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_Format;
    imageInfo.extent = { m_Width, m_Height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_Image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan storage image");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_Device, m_Image, &requirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_Memory) != VK_SUCCESS ||
        vkBindImageMemory(m_Device, m_Image, m_Memory, 0) != VK_SUCCESS)
    {
        Destroy();
        throw std::runtime_error("Failed to allocate Vulkan storage image memory");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_Format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
    {
        Destroy();
        throw std::runtime_error("Failed to create Vulkan storage image view");
    }

    TransitionToGeneral();
}

void VulkanStorageImage::Destroy()
{
    if (m_Device == VK_NULL_HANDLE)
        return;
    if (m_ImageView != VK_NULL_HANDLE)
        vkDestroyImageView(m_Device, m_ImageView, nullptr);
    if (m_Image != VK_NULL_HANDLE)
        vkDestroyImage(m_Device, m_Image, nullptr);
    if (m_Memory != VK_NULL_HANDLE)
        vkFreeMemory(m_Device, m_Memory, nullptr);
    m_ImageView = VK_NULL_HANDLE;
    m_Image = VK_NULL_HANDLE;
    m_Memory = VK_NULL_HANDLE;
    m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

uint32_t VulkanStorageImage::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    throw std::runtime_error("No compatible Vulkan storage image memory type");
}

void VulkanStorageImage::TransitionToGeneral()
{
    auto& context = RHIContext::Get();
    auto* vkContext = context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
    if (!vkContext || m_Image == VK_NULL_HANDLE)
        throw std::runtime_error("VulkanStorageImage requires an active Vulkan context");

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = vkContext->GetGraphicsQueueFamily();
    if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create storage image transition command pool");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer) != VK_SUCCESS)
    {
        vkDestroyCommandPool(m_Device, commandPool, nullptr);
        throw std::runtime_error("Failed to allocate storage image transition command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    const VkResult submitResult = vkQueueSubmit(
        vkContext->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    if (submitResult == VK_SUCCESS)
        vkQueueWaitIdle(vkContext->GetGraphicsQueue());
    vkDestroyCommandPool(m_Device, commandPool, nullptr);
    if (submitResult != VK_SUCCESS)
        throw std::runtime_error("Failed to transition Vulkan storage image");
    m_Layout = VK_IMAGE_LAYOUT_GENERAL;
}
void VulkanStorageImage::BindAsImage(uint32_t, ImageAccess)
{
    // Vulkan binds storage images through VkDescriptorSet, not image units.
}

void VulkanStorageImage::UnbindAsImage(uint32_t)
{
}

void VulkanStorageImage::BindAsTexture(uint32_t)
{
    // Vulkan binds sampled images through VkDescriptorSet, not texture units.
}

void VulkanStorageImage::Resize(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0 || (w == m_Width && h == m_Height))
        return;
    if (m_Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_Device);
    const VkFormat format = m_Format;
    Destroy();
    StorageImageDesc desc;
    desc.width = w;
    desc.height = h;
    desc.format = format == VK_FORMAT_R8_UNORM ? Format::R8_UNORM :
                  format == VK_FORMAT_R16_SFLOAT ? Format::R16F :
                  format == VK_FORMAT_R16G16_SFLOAT ? Format::RG16F :
                  format == VK_FORMAT_R16G16B16A16_SFLOAT ? Format::RGBA16F :
                  format == VK_FORMAT_R32_UINT ? Format::R32_UINT :
                  format == VK_FORMAT_R32_SINT ? Format::R32_SINT : Format::RGBA32F;
    Create(desc);
}
#endif
