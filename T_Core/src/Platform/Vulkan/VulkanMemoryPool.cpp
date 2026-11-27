#include "VulkanMemoryPool.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace
{
    struct FreeRange
    {
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
    };

    struct MemoryBlock
    {
        uint32_t id = 0;
        VkDevice device = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        VkDeviceSize usedBytes = 0;
        uint32_t memoryTypeIndex = 0;
        uint32_t allocationCount = 0;
        bool hostVisible = false;
        bool isDedicated = false;
        void* mapped = nullptr;
        std::vector<FreeRange> freeRanges;
    };

    std::mutex s_PoolMutex;
    std::vector<MemoryBlock> s_Blocks;
    uint32_t s_NextBlockId = 1;

    VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
    {
        if (alignment <= 1)
            return value;
        return ((value + alignment - 1) / alignment) * alignment;
    }

    void DestroyBlockMemory(MemoryBlock& block)
    {
        if (block.device == VK_NULL_HANDLE || block.memory == VK_NULL_HANDLE)
            return;
        if (block.mapped)
        {
            vkUnmapMemory(block.device, block.memory);
            block.mapped = nullptr;
        }
        vkFreeMemory(block.device, block.memory, nullptr);
        block.memory = VK_NULL_HANDLE;
    }

    bool TryAllocateFromBlock(
        MemoryBlock& block,
        VkDeviceSize reqSize,
        VkDeviceSize alignment,
        VulkanMemoryAllocation& outAlloc)
    {
        size_t bestIdx = std::numeric_limits<size_t>::max();
        VkDeviceSize bestWaste = std::numeric_limits<VkDeviceSize>::max();
        VkDeviceSize bestAlignedOffset = 0;

        for (size_t i = 0; i < block.freeRanges.size(); ++i)
        {
            const FreeRange& range = block.freeRanges[i];
            const VkDeviceSize alignedOffset = AlignUp(range.offset, alignment);
            if (alignedOffset < range.offset)
                continue;
            const VkDeviceSize padding = alignedOffset - range.offset;
            if (range.size < padding || range.size - padding < reqSize)
                continue;

            const VkDeviceSize waste = range.size - reqSize;
            if (waste < bestWaste)
            {
                bestWaste = waste;
                bestIdx = i;
                bestAlignedOffset = alignedOffset;
                if (waste == 0)
                    break;
            }
        }

        if (bestIdx == std::numeric_limits<size_t>::max())
            return false;

        const FreeRange chosen = block.freeRanges[bestIdx];
        const VkDeviceSize padding = bestAlignedOffset - chosen.offset;
        const VkDeviceSize tailOffset = bestAlignedOffset + reqSize;
        const VkDeviceSize tailSize = (chosen.offset + chosen.size) - tailOffset;

        block.freeRanges.erase(block.freeRanges.begin() + static_cast<ptrdiff_t>(bestIdx));
        size_t insertPos = bestIdx;
        if (padding > 0)
        {
            block.freeRanges.insert(
                block.freeRanges.begin() + static_cast<ptrdiff_t>(insertPos),
                FreeRange{ chosen.offset, padding });
            ++insertPos;
        }
        if (tailSize > 0)
        {
            block.freeRanges.insert(
                block.freeRanges.begin() + static_cast<ptrdiff_t>(insertPos),
                FreeRange{ tailOffset, tailSize });
        }

        block.usedBytes += reqSize;
        ++block.allocationCount;

        outAlloc.memory = block.memory;
        outAlloc.offset = bestAlignedOffset;
        outAlloc.size = reqSize;
        outAlloc.mapped = block.mapped
            ? static_cast<char*>(block.mapped) + bestAlignedOffset
            : nullptr;
        outAlloc.blockId = block.id;
        outAlloc.memoryTypeIndex = block.memoryTypeIndex;
        outAlloc.isDedicated = block.isDedicated;
        return true;
    }

    MemoryBlock CreateBlock(
        VkDevice device,
        VkDeviceSize blockSize,
        uint32_t memoryTypeIndex,
        bool hostVisible,
        bool isDedicated)
    {
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = blockSize;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
            return {};

        void* mapped = nullptr;
        if (hostVisible)
        {
            if (vkMapMemory(device, memory, 0, blockSize, 0, &mapped) != VK_SUCCESS)
            {
                vkFreeMemory(device, memory, nullptr);
                return {};
            }
        }

        MemoryBlock block;
        block.id = s_NextBlockId++;
        block.device = device;
        block.memory = memory;
        block.size = blockSize;
        block.usedBytes = 0;
        block.memoryTypeIndex = memoryTypeIndex;
        block.allocationCount = 0;
        block.hostVisible = hostVisible;
        block.isDedicated = isDedicated;
        block.mapped = mapped;
        block.freeRanges.push_back(FreeRange{ 0, blockSize });
        return block;
    }

    void InsertAndCoalesce(std::vector<FreeRange>& freeRanges, FreeRange range)
    {
        auto it = std::lower_bound(
            freeRanges.begin(),
            freeRanges.end(),
            range.offset,
            [](const FreeRange& lhs, VkDeviceSize offset)
            {
                return lhs.offset < offset;
            });

        it = freeRanges.insert(it, range);

        // Coalesce with next range
        auto next = std::next(it);
        if (next != freeRanges.end() && it->offset + it->size == next->offset)
        {
            it->size += next->size;
            freeRanges.erase(next);
        }

        // Coalesce with previous range
        if (it != freeRanges.begin())
        {
            auto prev = std::prev(it);
            if (prev->offset + prev->size == it->offset)
            {
                prev->size += it->size;
                freeRanges.erase(it);
            }
        }
    }

    bool HasAnotherEmptyPoolBlock(VkDevice device, uint32_t memoryTypeIndex, uint32_t excludeId)
    {
        for (const MemoryBlock& other : s_Blocks)
        {
            if (other.id != excludeId &&
                other.device == device &&
                other.memoryTypeIndex == memoryTypeIndex &&
                !other.isDedicated &&
                other.allocationCount == 0)
            {
                return true;
            }
        }
        return false;
    }
}

uint32_t VulkanMemoryPool::FindMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeBits,
    VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("No compatible Vulkan memory type");
}

VulkanMemoryAllocation VulkanMemoryPool::Allocate(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    const VkMemoryRequirements& requirements,
    VkMemoryPropertyFlags properties)
{
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || requirements.size == 0)
        throw std::runtime_error("Invalid Vulkan memory pool allocation request");

    const uint32_t memoryTypeIndex = FindMemoryType(
        physicalDevice, requirements.memoryTypeBits, properties);
    const bool hostVisible = (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    const VkDeviceSize alignment = std::max<VkDeviceSize>(requirements.alignment, 1);
    const bool preferDedicated = requirements.size > (kDefaultBlockSize / 2);

    std::lock_guard<std::mutex> lock(s_PoolMutex);

    VulkanMemoryAllocation alloc;
    if (!preferDedicated)
    {
        for (MemoryBlock& block : s_Blocks)
        {
            if (block.device != device ||
                block.memoryTypeIndex != memoryTypeIndex ||
                block.isDedicated)
            {
                continue;
            }
            if (TryAllocateFromBlock(block, requirements.size, alignment, alloc))
                return alloc;
        }

        MemoryBlock newBlock = CreateBlock(
            device, kDefaultBlockSize, memoryTypeIndex, hostVisible, false);
        if (newBlock.memory != VK_NULL_HANDLE)
        {
            s_Blocks.push_back(std::move(newBlock));
            if (TryAllocateFromBlock(s_Blocks.back(), requirements.size, alignment, alloc))
                return alloc;
        }
    }

    // Dedicated allocation (large buffer or fallback when pool block allocation fails)
    MemoryBlock dedicated = CreateBlock(
        device, requirements.size, memoryTypeIndex, hostVisible, true);
    if (dedicated.memory == VK_NULL_HANDLE)
        throw std::runtime_error("Failed to allocate Vulkan device memory");

    s_Blocks.push_back(std::move(dedicated));
    if (!TryAllocateFromBlock(s_Blocks.back(), requirements.size, alignment, alloc))
        throw std::runtime_error("Failed to sub-allocate dedicated Vulkan memory block");

    return alloc;
}

void VulkanMemoryPool::Free(VkDevice device, VulkanMemoryAllocation& allocation)
{
    if (!allocation.IsValid())
    {
        allocation = {};
        return;
    }

    std::lock_guard<std::mutex> lock(s_PoolMutex);

    for (size_t i = 0; i < s_Blocks.size(); ++i)
    {
        MemoryBlock& block = s_Blocks[i];
        if (block.id != allocation.blockId || block.device != device)
            continue;

        if (block.usedBytes >= allocation.size)
            block.usedBytes -= allocation.size;
        else
            block.usedBytes = 0;

        if (block.allocationCount > 0)
            --block.allocationCount;

        InsertAndCoalesce(block.freeRanges, FreeRange{ allocation.offset, allocation.size });

        const bool shouldFreeBlock =
            block.allocationCount == 0 &&
            (block.isDedicated || HasAnotherEmptyPoolBlock(device, block.memoryTypeIndex, block.id));
        if (shouldFreeBlock)
        {
            DestroyBlockMemory(block);
            s_Blocks.erase(s_Blocks.begin() + static_cast<ptrdiff_t>(i));
        }

        allocation = {};
        return;
    }

    // Block already freed during Shutdown
    allocation = {};
}

void VulkanMemoryPool::Shutdown(VkDevice device)
{
    if (device == VK_NULL_HANDLE)
        return;

    std::lock_guard<std::mutex> lock(s_PoolMutex);
    for (auto it = s_Blocks.begin(); it != s_Blocks.end();)
    {
        if (it->device == device)
        {
            DestroyBlockMemory(*it);
            it = s_Blocks.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

VulkanMemoryPoolStats VulkanMemoryPool::GetStats(VkDevice device)
{
    VulkanMemoryPoolStats stats;
    std::lock_guard<std::mutex> lock(s_PoolMutex);
    for (const MemoryBlock& block : s_Blocks)
    {
        if (device != VK_NULL_HANDLE && block.device != device)
            continue;
        ++stats.blockCount;
        if (block.isDedicated)
            ++stats.dedicatedBlockCount;
        stats.allocationCount += block.allocationCount;
        stats.totalAllocatedBytes += block.size;
        stats.totalUsedBytes += block.usedBytes;
    }
    return stats;
}
