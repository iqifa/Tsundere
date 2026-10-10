#pragma once

#include <mutex>
#include <vector>
#include <vulkan/vulkan.h>

// ---------------------------------------------------------------------------
// VulkanDeletionQueue — 延迟销毁仍被已录制命令缓冲引用的 Vulkan 对象
//
// 删子 mesh 发生在本帧 Draw 之后、vkQueueSubmit 之前。vkCmdBindVertexBuffers
// 记下的是 VkBuffer 句柄，立刻 vkDestroyBuffer 会让提交出去的命令碰到已销毁
// 的缓冲，驱动直接崩溃。OpenGL 的 glDelete* 由驱动推迟到 GPU 用完，所以那边正常。
//
// 队列在 BeginFrame 的 vkWaitForFences 之后 Flush：上一帧提交已经完成，
// 这时销毁才合法。Shutdown 在 WaitIdle 之后改为立即销毁，设备销毁后关闭队列。
// ---------------------------------------------------------------------------
class VulkanDeletionQueue
{
public:
    static void EnqueueBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
    {
        if (device == VK_NULL_HANDLE ||
            (buffer == VK_NULL_HANDLE && memory == VK_NULL_HANDLE))
        {
            return;
        }

        std::lock_guard<std::mutex> lock(s_Mutex);
        if (s_Closed)
            return;
        if (s_Immediate)
        {
            DestroyBuffer(device, buffer, memory);
            return;
        }
        s_Queue.push_back({ device, buffer, memory });
    }

    static void Flush()
    {
        std::vector<Item> pending;
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            pending.swap(s_Queue);
        }
        for (const Item& item : pending)
            DestroyBuffer(item.device, item.buffer, item.memory);
    }

    // Device is idle and still alive. Pending items are destroyed now;
    // later releases destroy immediately.
    static void BeginImmediate()
    {
        std::vector<Item> pending;
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            s_Immediate = true;
            pending.swap(s_Queue);
        }
        for (const Item& item : pending)
            DestroyBuffer(item.device, item.buffer, item.memory);
    }

    static bool IsClosed()
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        return s_Closed;
    }

    // Device has been destroyed. Further releases must not call Vulkan.
    static void Close()
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Closed = true;
        s_Immediate = false;
        s_Queue.clear();
    }

private:
    struct Item
    {
        VkDevice device = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    static void DestroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
    {
        if (buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, buffer, nullptr);
        if (memory != VK_NULL_HANDLE)
            vkFreeMemory(device, memory, nullptr);
    }

    static inline std::mutex s_Mutex;
    static inline std::vector<Item> s_Queue;
    static inline bool s_Immediate = false;
    static inline bool s_Closed = false;
};
