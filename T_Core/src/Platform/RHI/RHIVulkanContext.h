#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

// Forward declaration
class VulkanRenderPassCache;

// Vulkan-only native access. Keep Vulkan handles out of the common RHIContext
// interface so OpenGL code only depends on this header when Vulkan is selected.
class RHIVulkanContext
{
public:
    virtual ~RHIVulkanContext() = default;

    virtual VkInstance GetInstance() const = 0;
    virtual VkPhysicalDevice GetPhysicalDevice() const = 0;
    virtual VkDevice GetDevice() const = 0;
    virtual VkQueue GetGraphicsQueue() const = 0;
    virtual uint32_t GetGraphicsQueueFamily() const = 0;
    virtual VkCommandBuffer GetCurrentVkCommandBuffer() const = 0;
    virtual void BeginPresentPass() = 0;
    virtual void EndPresentPass() = 0;
    virtual VkRenderPass GetImGuiRenderPass() const = 0;
    virtual VkDescriptorPool GetImGuiDescriptorPool() const = 0;
    virtual uint32_t GetMinImageCount() const = 0;
    virtual uint32_t GetImageCount() const = 0;
    virtual uint64_t GetSwapchainGeneration() const = 0;
    virtual bool SupportsDynamicRendering() const { return false; }

    // Optional cache hook. Backends that use classic Vulkan render passes can
    // override this; a null cache keeps the native context usable while the
    // backend is assembled incrementally.
    virtual VulkanRenderPassCache* GetRenderPassCache() { return nullptr; }
};
