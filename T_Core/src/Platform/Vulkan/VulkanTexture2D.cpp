#include "VulkanTexture2D.h"
#include "Platform/RenderAPIConfig.h"

#ifdef RenderAPI_Vulkan
Ref<RHITexture2D> RHITexture2D::Create(const Texture2DDesc& desc)
{
    return CreateRef<VulkanTexture2D>(desc);
}
#endif

#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Debug/Debug.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    VkImageUsageFlags ToVkImageUsage(TextureUsage usage)
    {
        VkImageUsageFlags flags = 0;
        if (usage & TextureUsage::Sampled)
            flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (usage & TextureUsage::Storage)
            flags |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (usage & TextureUsage::ColorAttachment)
            flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (usage & TextureUsage::DepthStencil)
            flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (usage & TextureUsage::TransferSrc)
            flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (usage & TextureUsage::TransferDst)
            flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        return flags;
    }

    bool HasStencil(Format format)
    {
        return format == Format::D24_UNORM_S8_UINT ||
               format == Format::D32_SFLOAT_S8_UINT;
    }

    bool IsDepthFormat(Format format)
    {
        return format == Format::D24_UNORM_S8_UINT ||
               format == Format::D32_SFLOAT ||
               format == Format::D32_SFLOAT_S8_UINT;
    }
}

VulkanTexture2D::VulkanTexture2D(const Texture2DDesc& desc)
{
    Create(desc);
}

VulkanTexture2D::~VulkanTexture2D()
{
    Destroy();
}

void VulkanTexture2D::Create(const Texture2DDesc& desc)
{
    auto& context = RHIContext::Get();
    auto* vkContext = context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
    if (!vkContext)
        throw std::runtime_error("VulkanTexture2D requires an active Vulkan context");

    m_Device = vkContext->GetDevice();
    m_PhysicalDevice = vkContext->GetPhysicalDevice();
    m_Queue = vkContext->GetGraphicsQueue();
    m_QueueFamily = vkContext->GetGraphicsQueueFamily();
    m_Width = desc.width;
    m_Height = desc.height;
    m_SampleCount = desc.sampleCount;
    m_Usage = desc.usage;
    m_FilePath = desc.filePath;

    const Format resolvedFormat = IsDepthFormat(desc.format)
        ? VulkanPipelineUtil::ResolveDepthFormat(desc.format)
        : desc.format;
    m_Format = resolvedFormat;
    const VkFormat format = VulkanPipelineUtil::ToVkFormat(resolvedFormat);
    const VkSampleCountFlagBits samples = VulkanPipelineUtil::ToVkSampleCount(desc.sampleCount);
    const VkImageUsageFlags usage = ToVkImageUsage(desc.usage) |
        (desc.pixelData ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0);
    if (m_Device == VK_NULL_HANDLE || m_PhysicalDevice == VK_NULL_HANDLE ||
        desc.width == 0 || desc.height == 0 || format == VK_FORMAT_UNDEFINED ||
        samples == 0 || usage == 0)
    {
        throw std::runtime_error("Invalid Vulkan texture description");
    }

    m_AspectMask = IsDepthFormat(resolvedFormat)
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;
    if (HasStencil(resolvedFormat))
        m_AspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { desc.width, desc.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = samples;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_Image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan texture image");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_Device, m_Image, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_Memory) != VK_SUCCESS)
    {
        Destroy();
        throw std::runtime_error("Failed to allocate Vulkan texture memory");
    }

    if (vkBindImageMemory(m_Device, m_Image, m_Memory, 0) != VK_SUCCESS)
    {
        Destroy();
        throw std::runtime_error("Failed to bind Vulkan texture memory");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = m_AspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
    {
        Destroy();
        throw std::runtime_error("Failed to create Vulkan texture image view");
    }

    if (desc.usage & TextureUsage::Sampled)
    {
        auto toFilter = [](FilterMode filter)
        {
            return filter == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        };
        auto toAddressMode = [](WrapMode wrap)
        {
            switch (wrap)
            {
            case WrapMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case WrapMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            case WrapMode::ClampToEdge:
            default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            }
        };

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = toFilter(desc.magFilter);
        samplerInfo.minFilter = toFilter(desc.minFilter);
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = toAddressMode(desc.wrapS);
        samplerInfo.addressModeV = toAddressMode(desc.wrapT);
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = desc.borderColor[0] > 0.5f
            ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
            : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            Destroy();
            throw std::runtime_error("Failed to create Vulkan texture sampler");
        }
    }

    if (desc.pixelData)
        UploadPixels(desc, format);
}

void VulkanTexture2D::UploadPixels(const Texture2DDesc& desc, VkFormat format)
{
    if (desc.sampleCount != 1)
        throw std::runtime_error("VulkanTexture2D pixel uploads require single-sample images");

    const bool rgba = format == VK_FORMAT_R8G8B8A8_UNORM ||
                      format == VK_FORMAT_R8G8B8A8_SRGB;
    const bool red = format == VK_FORMAT_R8_UNORM;
    if (!rgba && !red)
        throw std::runtime_error("VulkanTexture2D pixel uploads currently support R8 and RGBA8 formats only");
    if (desc.dataChannels != 1 && desc.dataChannels != 3 && desc.dataChannels != 4)
        throw std::runtime_error("VulkanTexture2D pixel upload has unsupported channel count");

    const size_t pixelCount = static_cast<size_t>(m_Width) * m_Height;
    const size_t destinationChannels = rgba ? 4u : 1u;
    const size_t uploadSize = pixelCount * destinationChannels;
    std::vector<unsigned char> converted(uploadSize);
    const auto* source = static_cast<const unsigned char*>(desc.pixelData);

    if (rgba && desc.dataChannels == 4)
    {
        std::memcpy(converted.data(), source, uploadSize);
    }
    else
    {
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const unsigned char* src = source + i * desc.dataChannels;
            if (rgba)
            {
                unsigned char* dst = converted.data() + i * 4;
                dst[0] = src[0];
                dst[1] = desc.dataChannels > 1 ? src[1] : src[0];
                dst[2] = desc.dataChannels > 1 ? src[2] : src[0];
                dst[3] = desc.dataChannels == 4 ? src[3] : 255;
            }
            else
            {
                converted[i] = src[0];
            }
        }
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    auto destroyBuffer = [&]
    {
        if (stagingBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
        if (stagingMemory != VK_NULL_HANDLE)
            vkFreeMemory(m_Device, stagingMemory, nullptr);
    };

    try
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = uploadSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan texture staging buffer");

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(m_Device, stagingBuffer, &requirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan texture staging memory");
        if (vkBindBufferMemory(m_Device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS)
            throw std::runtime_error("Failed to bind Vulkan texture staging memory");

        void* mapped = nullptr;
        if (vkMapMemory(m_Device, stagingMemory, 0, uploadSize, 0, &mapped) != VK_SUCCESS)
            throw std::runtime_error("Failed to map Vulkan texture staging memory");
        std::memcpy(mapped, converted.data(), uploadSize);
        vkUnmapMemory(m_Device, stagingMemory);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_QueueFamily;
        if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan texture upload command pool");

        VkCommandBufferAllocateInfo commandAlloc{};
        commandAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandAlloc.commandPool = commandPool;
        commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAlloc.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(m_Device, &commandAlloc, &commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan texture upload command buffer");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin Vulkan texture upload commands");

        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = m_Image;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &toTransfer);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { m_Width, m_Height, 1 };
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toShaderRead = toTransfer;
        toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to end Vulkan texture upload commands");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        if (vkQueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS ||
            vkQueueWaitIdle(m_Queue) != VK_SUCCESS)
            throw std::runtime_error("Failed to submit Vulkan texture upload commands");

        m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    catch (...)
    {
        if (commandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_Device, commandPool, nullptr);
        destroyBuffer();
        throw;
    }

    vkDestroyCommandPool(m_Device, commandPool, nullptr);
    destroyBuffer();
}
void VulkanTexture2D::Destroy()
{
    if (m_Device == VK_NULL_HANDLE)
        return;

    if (m_Sampler != VK_NULL_HANDLE)
        vkDestroySampler(m_Device, m_Sampler, nullptr);
    if (m_ImageView != VK_NULL_HANDLE)
        vkDestroyImageView(m_Device, m_ImageView, nullptr);
    if (m_Image != VK_NULL_HANDLE)
        vkDestroyImage(m_Device, m_Image, nullptr);
    if (m_Memory != VK_NULL_HANDLE)
        vkFreeMemory(m_Device, m_Memory, nullptr);

    m_Sampler = VK_NULL_HANDLE;
    m_ImageView = VK_NULL_HANDLE;
    m_Image = VK_NULL_HANDLE;
    m_Memory = VK_NULL_HANDLE;
    m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

uint32_t VulkanTexture2D::FindMemoryType(
    uint32_t typeBits,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    throw std::runtime_error("No compatible Vulkan memory type");
}

void VulkanTexture2D::Bind(uint32_t slot)
{
    (void)slot;
}

void VulkanTexture2D::Unbind() {}

void VulkanTexture2D::BindAsImage(uint32_t slot, ImageAccess access)
{
    (void)slot;
    (void)access;
}

void VulkanTexture2D::UnbindAsImage(uint32_t slot)
{
    (void)slot;
}

void VulkanTexture2D::Resize(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0 || (w == m_Width && h == m_Height))
        return;

    Texture2DDesc desc;
    desc.width = w;
    desc.height = h;
    desc.format = m_Format;
    desc.sampleCount = m_SampleCount;
    desc.usage = m_Usage;
    desc.filePath = m_FilePath;

    if (m_Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_Device);
    Destroy();
    Create(desc);
}
