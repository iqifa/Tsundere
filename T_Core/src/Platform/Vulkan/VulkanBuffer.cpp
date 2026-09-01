#include "VulkanBuffer.h"

#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include "Platform/RenderAPIConfig.h"

#ifdef RenderAPI_Vulkan
Ref<RHIBuffer> RHIBuffer::Create(const BufferDesc& desc)
{
    return CreateRef<VulkanBuffer>(desc);
}
#endif

#include <cstring>
#include <stdexcept>

namespace
{
    VkBufferUsageFlags ToVkBufferUsage(BufferUsage usage)
    {
        switch (usage)
        {
        case BufferUsage::Vertex: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        case BufferUsage::Index: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        case BufferUsage::Uniform: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        case BufferUsage::Storage: return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        case BufferUsage::Staging: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        }
        return 0;
    }

    void DestroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
    {
        if (buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, buffer, nullptr);
        if (memory != VK_NULL_HANDLE)
            vkFreeMemory(device, memory, nullptr);
    }
}

VulkanBuffer::VulkanBuffer(const BufferDesc& desc)
{
    Create(desc);
    if (desc.initialData && desc.size)
        Upload(desc.initialData, desc.size);
}

VulkanBuffer::~VulkanBuffer()
{
    Destroy();
}

void VulkanBuffer::Create(const BufferDesc& desc)
{
    auto& context = RHIContext::Get();
    auto* vkContext = context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
    if (!vkContext || desc.size == 0)
        throw std::runtime_error("Invalid Vulkan buffer description");

    m_Device = vkContext->GetDevice();
    m_PhysicalDevice = vkContext->GetPhysicalDevice();
    m_Queue = vkContext->GetGraphicsQueue();
    m_QueueFamily = vkContext->GetGraphicsQueueFamily();
    m_Size = desc.size;
    m_Usage = desc.usage;
    m_HostVisible = desc.cpuAccess || desc.usage == BufferUsage::Staging;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = ToVkBufferUsage(desc.usage);
    if (!m_HostVisible)
        bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (bufferInfo.usage == 0 || vkCreateBuffer(m_Device, &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan buffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_Device, m_Buffer, &requirements);

    VkMemoryPropertyFlags properties = m_HostVisible
        ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_Memory) != VK_SUCCESS ||
        vkBindBufferMemory(m_Device, m_Buffer, m_Memory, 0) != VK_SUCCESS)
    {
        Destroy();
        throw std::runtime_error("Failed to allocate Vulkan buffer memory");
    }

    if (m_HostVisible)
        vkMapMemory(m_Device, m_Memory, 0, m_Size, 0, &m_Mapped);
}

void VulkanBuffer::Destroy()
{
    if (m_Device == VK_NULL_HANDLE)
        return;
    if (m_Mapped)
        vkUnmapMemory(m_Device, m_Memory);
    if (m_Buffer)
        vkDestroyBuffer(m_Device, m_Buffer, nullptr);
    if (m_Memory)
        vkFreeMemory(m_Device, m_Memory, nullptr);
    m_Mapped = nullptr;
    m_Buffer = VK_NULL_HANDLE;
    m_Memory = VK_NULL_HANDLE;
}

uint32_t VulkanBuffer::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    throw std::runtime_error("No compatible Vulkan buffer memory type");
}

void VulkanBuffer::Upload(const void* data, uint32_t size, uint32_t offset)
{
    if (!data || offset > m_Size || size > m_Size - offset)
        return;

    if (m_HostVisible)
    {
        if (!m_Mapped)
            return;
        std::memcpy(static_cast<char*>(m_Mapped) + offset, data, size);
        return;
    }

    if (m_Device == VK_NULL_HANDLE || m_Queue == VK_NULL_HANDLE ||
        m_Buffer == VK_NULL_HANDLE || m_QueueFamily == VK_QUEUE_FAMILY_IGNORED)
        return;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    try
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan buffer staging buffer");

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(m_Device, stagingBuffer, &requirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan buffer staging memory");
        if (vkBindBufferMemory(m_Device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS)
            throw std::runtime_error("Failed to bind Vulkan buffer staging memory");

        void* mapped = nullptr;
        if (vkMapMemory(m_Device, stagingMemory, 0, size, 0, &mapped) != VK_SUCCESS)
            throw std::runtime_error("Failed to map Vulkan buffer staging memory");
        std::memcpy(mapped, data, size);
        vkUnmapMemory(m_Device, stagingMemory);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_QueueFamily;
        if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan buffer upload command pool");

        VkCommandBufferAllocateInfo commandAlloc{};
        commandAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandAlloc.commandPool = commandPool;
        commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAlloc.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(m_Device, &commandAlloc, &commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan buffer upload command buffer");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin Vulkan buffer upload commands");

        VkBufferCopy copy{};
        copy.srcOffset = 0;
        copy.dstOffset = offset;
        copy.size = size;
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_Buffer, 1, &copy);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to end Vulkan buffer upload commands");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        if (vkQueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS ||
            vkQueueWaitIdle(m_Queue) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to submit Vulkan buffer upload commands");
        }
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

void* VulkanBuffer::Map()
{
    if (!m_HostVisible)
        return nullptr;
    if (!m_Mapped)
        vkMapMemory(m_Device, m_Memory, 0, m_Size, 0, &m_Mapped);
    return m_Mapped;
}

void VulkanBuffer::Unmap()
{
    // Keep persistently mapped host-visible buffers available for subsequent uploads.
}
