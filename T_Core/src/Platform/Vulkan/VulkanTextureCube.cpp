#include "VulkanTextureCube.h"

#include "Debug/Debug.h"
#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include "Platform/RenderAPIConfig.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "stb_image/stb_image.h"

#include <array>
#include <cstring>
#include <stdexcept>

#ifdef RenderAPI_Vulkan
Ref<RHITextureCube> RHITextureCube::Create(const TextureCubeDesc& desc)
{
    return CreateRef<VulkanTextureCube>(desc);
}
#endif

namespace
{
    VkFilter ToVkFilter(FilterMode filter)
    {
        return filter == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    }

    void DestroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
    {
        if (buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, buffer, nullptr);
        if (memory != VK_NULL_HANDLE)
            vkFreeMemory(device, memory, nullptr);
    }
}

VulkanTextureCube::VulkanTextureCube(const TextureCubeDesc& desc)
{
    Create(desc);
}

VulkanTextureCube::~VulkanTextureCube()
{
    Destroy();
}

void VulkanTextureCube::Create(const TextureCubeDesc& desc)
{
    if (desc.facePaths.size() != 6)
        throw std::runtime_error("VulkanTextureCube requires exactly six face paths");

    auto& context = RHIContext::Get();
    auto* vkContext = context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
    if (!vkContext)
        throw std::runtime_error("VulkanTextureCube requires an active Vulkan context");

    m_Device = vkContext->GetDevice();
    m_PhysicalDevice = vkContext->GetPhysicalDevice();
    m_Queue = vkContext->GetGraphicsQueue();
    m_QueueFamily = vkContext->GetGraphicsQueueFamily();
    if (m_Device == VK_NULL_HANDLE || m_PhysicalDevice == VK_NULL_HANDLE ||
        m_Queue == VK_NULL_HANDLE)
    {
        throw std::runtime_error("VulkanTextureCube requires valid Vulkan device and queue handles");
    }

    const VkFormat format = VulkanPipelineUtil::ToVkFormat(desc.format);
    if (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R8G8B8A8_SRGB)
        throw std::runtime_error("VulkanTextureCube currently supports RGBA8 formats only");

    std::array<unsigned char*, 6> loadedFaces{};
    std::array<int, 6> widths{};
    std::array<int, 6> heights{};
    std::vector<unsigned char> pixels;

    try
    {
        for (uint32_t face = 0; face < 6; ++face)
        {
            int channels = 0;
            loadedFaces[face] = stbi_load(
                desc.facePaths[face].c_str(),
                &widths[face],
                &heights[face],
                &channels,
                STBI_rgb_alpha);

            if (!loadedFaces[face])
            {
                throw std::runtime_error(
                    "Failed to load Vulkan cubemap face: " + desc.facePaths[face]);
            }

            if (widths[face] <= 0 || heights[face] <= 0 || widths[face] != heights[face])
                throw std::runtime_error("Vulkan cubemap faces must be non-empty square images");

            if (face > 0 &&
                (widths[face] != widths[0] || heights[face] != heights[0]))
            {
                throw std::runtime_error("Vulkan cubemap faces must have matching dimensions");
            }
        }

        m_Size = static_cast<uint32_t>(widths[0]);
        if (desc.size != 0 && desc.size != m_Size)
            throw std::runtime_error("Vulkan cubemap face size does not match TextureCubeDesc::size");

        const size_t faceBytes = static_cast<size_t>(m_Size) * m_Size * 4;
        pixels.resize(faceBytes * 6);
        for (uint32_t face = 0; face < 6; ++face)
            std::memcpy(pixels.data() + faceBytes * face, loadedFaces[face], faceBytes);
    }
    catch (...)
    {
        for (unsigned char* face : loadedFaces)
        {
            if (face)
                stbi_image_free(face);
        }
        throw;
    }

    for (unsigned char* face : loadedFaces)
        stbi_image_free(face);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { m_Size, m_Size, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_Image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan cubemap image");

    try
    {
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(m_Device, m_Image, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_Memory) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan cubemap memory");
        if (vkBindImageMemory(m_Device, m_Image, m_Memory, 0) != VK_SUCCESS)
            throw std::runtime_error("Failed to bind Vulkan cubemap memory");

        Upload(pixels, static_cast<uint32_t>(pixels.size() / 6));

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;
        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan cubemap image view");

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = ToVkFilter(desc.magFilter);
        samplerInfo.minFilter = ToVkFilter(desc.minFilter);
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan cubemap sampler");
    }
    catch (...)
    {
        Destroy();
        throw;
    }
}

void VulkanTextureCube::Upload(
    const std::vector<unsigned char>& pixels,
    uint32_t faceSize)
{
    const VkDeviceSize uploadSize = static_cast<VkDeviceSize>(faceSize) * 6;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    try
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = uploadSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan cubemap staging buffer");

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(m_Device, stagingBuffer, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan cubemap staging memory");
        if (vkBindBufferMemory(m_Device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS)
            throw std::runtime_error("Failed to bind Vulkan cubemap staging memory");

        void* mapped = nullptr;
        if (vkMapMemory(m_Device, stagingMemory, 0, uploadSize, 0, &mapped) != VK_SUCCESS)
            throw std::runtime_error("Failed to map Vulkan cubemap staging memory");
        std::memcpy(mapped, pixels.data(), static_cast<size_t>(uploadSize));
        vkUnmapMemory(m_Device, stagingMemory);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_QueueFamily;
        if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan cubemap upload command pool");

        VkCommandBufferAllocateInfo commandAlloc{};
        commandAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandAlloc.commandPool = commandPool;
        commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAlloc.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(m_Device, &commandAlloc, &commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan cubemap upload command buffer");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin Vulkan cubemap upload commands");

        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = 0;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = m_Image;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.baseMipLevel = 0;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.baseArrayLayer = 0;
        toTransfer.subresourceRange.layerCount = 6;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toTransfer);

        std::array<VkBufferImageCopy, 6> regions{};
        for (uint32_t face = 0; face < 6; ++face)
        {
            regions[face].bufferOffset = static_cast<VkDeviceSize>(faceSize) * face;
            regions[face].bufferRowLength = 0;
            regions[face].bufferImageHeight = 0;
            regions[face].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[face].imageSubresource.mipLevel = 0;
            regions[face].imageSubresource.baseArrayLayer = face;
            regions[face].imageSubresource.layerCount = 1;
            regions[face].imageOffset = { 0, 0, 0 };
            regions[face].imageExtent = { m_Size, m_Size, 1 };
        }
        vkCmdCopyBufferToImage(
            commandBuffer,
            stagingBuffer,
            m_Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(regions.size()),
            regions.data());

        VkImageMemoryBarrier toShaderRead{};
        toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.image = m_Image;
        toShaderRead.subresourceRange = toTransfer.subresourceRange;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &toShaderRead);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to end Vulkan cubemap upload commands");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        if (vkQueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS ||
            vkQueueWaitIdle(m_Queue) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to submit Vulkan cubemap upload commands");
        }

        m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    catch (...)
    {
        if (commandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_Device, commandPool, nullptr);
        DestroyBuffer(m_Device, stagingBuffer, stagingMemory);
        throw;
    }

    vkDestroyCommandPool(m_Device, commandPool, nullptr);
    DestroyBuffer(m_Device, stagingBuffer, stagingMemory);
}

void VulkanTextureCube::Destroy()
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

uint32_t VulkanTextureCube::FindMemoryType(
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
    throw std::runtime_error("No compatible Vulkan cubemap memory type");
}

void VulkanTextureCube::Bind(uint32_t slot)
{
    (void)slot;
}
