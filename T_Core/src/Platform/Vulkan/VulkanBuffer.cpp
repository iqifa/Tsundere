#include "VulkanBuffer.h"

#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include "Platform/RenderAPIConfig.h"
#include "Platform/Vulkan/VulkanDeletionQueue.h"

#ifdef RenderAPI_Vulkan
Ref<RHIBuffer> RHIBuffer::Create(const BufferDesc& desc)
{
    return CreateRef<VulkanBuffer>(desc);
}
#endif

#include <cstring>
#include <stdexcept>
#include <utility>

namespace
{
    VkBufferUsageFlags ToVkBufferUsage(BufferUsage usage)
    {
        switch (usage)
        {
        case BufferUsage::Vertex: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        case BufferUsage::Index: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        case BufferUsage::Uniform: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        case BufferUsage::Storage: return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        case BufferUsage::Staging: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        }
        return 0;
    }

    void DestroyStagingBuffer(VkDevice device, VkBuffer buffer, VulkanMemoryAllocation& alloc)
    {
        if (buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, buffer, nullptr);
        VulkanMemoryPool::Free(device, alloc);
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

    try
    {
        m_Allocation = VulkanMemoryPool::Allocate(m_Device, m_PhysicalDevice, requirements, properties);
        if (vkBindBufferMemory(m_Device, m_Buffer, m_Allocation.memory, m_Allocation.offset) != VK_SUCCESS)
            throw std::runtime_error("Failed to bind Vulkan buffer memory");
    }
    catch (...)
    {
        Destroy();
        throw;
    }

    if (m_HostVisible)
        m_Mapped = m_Allocation.mapped;
}

void VulkanBuffer::Destroy()
{
    if (m_Device == VK_NULL_HANDLE)
        return;
    if (VulkanDeletionQueue::IsClosed())
    {
        m_Mapped = nullptr;
        m_Buffer = VK_NULL_HANDLE;
        m_Allocation = {};
        m_Device = VK_NULL_HANDLE;
        return;
    }

    const VkDevice device = m_Device;
    const VkBuffer buffer = m_Buffer;
    VulkanMemoryAllocation allocation = m_Allocation;
    m_Mapped = nullptr;
    m_Buffer = VK_NULL_HANDLE;
    m_Allocation = {};
    m_Device = VK_NULL_HANDLE;

    // The frame command buffer may already contain vkCmdBind*Buffer for this
    // allocation (submesh delete runs after Execute, before submit).
    VulkanDeletionQueue::EnqueueCallback([device, buffer, allocation]() mutable {
        if (buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, buffer, nullptr);
        VulkanMemoryPool::Free(device, allocation);
    });
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

    UploadRegions({ BufferUploadRegion{ this, data, size, offset } });
}

#ifdef RenderAPI_Vulkan
void RHIBuffer::UploadBatch(const std::vector<BufferUploadRegion>& regions)
{
    VulkanBuffer::UploadRegions(regions);
}
#endif

void VulkanBuffer::UploadRegions(const std::vector<BufferUploadRegion>& regions)
{
    // ---- 1. 全部校验通过之前不写任何数据 ----
    struct Copy
    {
        VulkanBuffer* dst = nullptr;
        const BufferUploadRegion* region = nullptr;
        VkDeviceSize stagingOffset = 0;
    };
    std::vector<Copy> copies;
    std::vector<std::pair<VulkanBuffer*, const BufferUploadRegion*>> hostWrites;
    VkDeviceSize stagingBytes = 0;
    VulkanBuffer* first = nullptr;
    for (const BufferUploadRegion& region : regions)
    {
        auto* vk = dynamic_cast<VulkanBuffer*>(region.buffer);
        if (!vk || !region.data || region.offset > vk->m_Size || region.size > vk->m_Size - region.offset)
            throw std::invalid_argument("UploadBatch: invalid region");
        if (region.size == 0)
            continue;
        if (vk->m_HostVisible)
        {
            if (!vk->m_Mapped)
                throw std::invalid_argument("UploadBatch: host-visible buffer is not mapped");
            hostWrites.emplace_back(vk, &region);
            continue;
        }
        if (vk->m_Device == VK_NULL_HANDLE || vk->m_Queue == VK_NULL_HANDLE ||
            vk->m_Buffer == VK_NULL_HANDLE || vk->m_QueueFamily == VK_QUEUE_FAMILY_IGNORED)
            throw std::invalid_argument("UploadBatch: buffer has no device");
        if (first && (vk->m_Device != first->m_Device || vk->m_Queue != first->m_Queue))
            throw std::invalid_argument("UploadBatch: regions span devices or queues");
        if (!first)
            first = vk;
        stagingBytes = (stagingBytes + 15) & ~VkDeviceSize(15);
        copies.push_back({ vk, &region, stagingBytes });
        stagingBytes += region.size;
    }

    for (const auto& write : hostWrites)
        std::memcpy(static_cast<char*>(write.first->m_Mapped) + write.second->offset, write.second->data, write.second->size);
    if (copies.empty())
        return;

    // ---- 2. 一个暂存 buffer + 一个命令缓冲：所有拷贝一次提交、一次等待 ----
    const VkDevice device = first->m_Device;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VulkanMemoryAllocation stagingAlloc{};
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    try
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = stagingBytes;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan buffer staging buffer");

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, stagingBuffer, &requirements);
        stagingAlloc = VulkanMemoryPool::Allocate(
            device,
            first->m_PhysicalDevice,
            requirements,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkBindBufferMemory(device, stagingBuffer, stagingAlloc.memory, stagingAlloc.offset) != VK_SUCCESS)
            throw std::runtime_error("Failed to bind Vulkan buffer staging memory");

        if (!stagingAlloc.mapped)
            throw std::runtime_error("Vulkan staging memory block is not mapped");
        for (const Copy& copy : copies)
            std::memcpy(static_cast<char*>(stagingAlloc.mapped) + copy.stagingOffset, copy.region->data, copy.region->size);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = first->m_QueueFamily;
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan buffer upload command pool");

        VkCommandBufferAllocateInfo commandAlloc{};
        commandAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandAlloc.commandPool = commandPool;
        commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAlloc.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &commandAlloc, &commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan buffer upload command buffer");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin Vulkan buffer upload commands");

        // 局部覆盖（BVH refit 等）时目标 buffer 可能仍被之前提交的帧读取：
        // 拷贝前等同一队列上先前的命令结束（WAR），拷贝后让写入对后续任意读取可见。
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr);

        for (const Copy& copy : copies)
        {
            VkBufferCopy region{};
            region.srcOffset = copy.stagingOffset;
            region.dstOffset = copy.region->offset;
            region.size = copy.region->size;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer, copy.dst->m_Buffer, 1, &region);
        }

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to end Vulkan buffer upload commands");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        if (vkQueueSubmit(first->m_Queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS ||
            vkQueueWaitIdle(first->m_Queue) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to submit Vulkan buffer upload commands");
        }
    }
    catch (...)
    {
        if (commandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, commandPool, nullptr);
        DestroyStagingBuffer(device, stagingBuffer, stagingAlloc);
        throw;
    }

    vkDestroyCommandPool(device, commandPool, nullptr);
    DestroyStagingBuffer(device, stagingBuffer, stagingAlloc);
}

void* VulkanBuffer::Map()
{
    if (!m_HostVisible)
        return nullptr;
    return m_Mapped;
}

void VulkanBuffer::Unmap()
{
    // Keep persistently mapped host-visible buffers available for subsequent uploads.
}
