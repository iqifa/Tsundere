#pragma once

#include "Core/Core.h"
#include <vulkan/vulkan.h>
#include <cstdint>

struct VulkanMemoryAllocation
{
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
    uint32_t blockId = 0;
    uint32_t memoryTypeIndex = 0;
    bool isDedicated = false;

    bool IsValid() const { return memory != VK_NULL_HANDLE && size > 0; }
};

struct VulkanMemoryPoolStats
{
    uint32_t blockCount = 0;
    uint32_t dedicatedBlockCount = 0;
    uint32_t allocationCount = 0;
    VkDeviceSize totalAllocatedBytes = 0;
    VkDeviceSize totalUsedBytes = 0;
};

// Sub-allocator for VkDeviceMemory (Plan A):
// Allocates large VkDeviceMemory blocks per memoryTypeIndex and sub-allocates
// aligned byte ranges for individual VkBuffers. Keeps all free-list metadata
// on the CPU and persistently maps HOST_VISIBLE blocks once at block creation.
class T_API VulkanMemoryPool
{
public:
    static constexpr VkDeviceSize kDefaultBlockSize = 64ull * 1024ull * 1024ull; // 64 MB

    static uint32_t FindMemoryType(
        VkPhysicalDevice physicalDevice,
        uint32_t typeBits,
        VkMemoryPropertyFlags properties);

    static VulkanMemoryAllocation Allocate(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        const VkMemoryRequirements& requirements,
        VkMemoryPropertyFlags properties);

    static void Free(VkDevice device, VulkanMemoryAllocation& allocation);

    // Frees all blocks belonging to `device` (must be called before vkDestroyDevice).
    static void Shutdown(VkDevice device);

    static VulkanMemoryPoolStats GetStats(VkDevice device = VK_NULL_HANDLE);
};
