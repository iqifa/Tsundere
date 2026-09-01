#pragma once

#include <vulkan/vulkan_core.h>
#include <Platform/RHI/RHIPipeline.h>
#include <unordered_map>
#include <vector>

// Hash function for RenderingSignature
namespace std {
    template<>
    struct hash<RenderingSignature>
    {
        size_t operator()(const RenderingSignature& config) const noexcept
        {
            size_t seed = 0;
            for (const auto& fmt : config.colorFormats)
            {
                seed ^= std::hash<uint8_t>{}(static_cast<uint8_t>(fmt)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            seed ^= std::hash<uint8_t>{}(static_cast<uint8_t>(config.depthFormat)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            // Hash sample count and view mask as part of compatibility.
            seed ^= std::hash<uint32_t>{}(config.sampleCount) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<uint32_t>{}(config.viewMask) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
}

// VulkanRenderPassCache - creates and caches VkRenderPass objects
// based on attachment configurations
class VulkanRenderPassCache
{
public:
    VulkanRenderPassCache(VkDevice device);
    ~VulkanRenderPassCache();

    // Get or create a render pass for the given attachment configuration
    VkRenderPass GetRenderPass(const RenderingSignature& config);

    // Clear all cached render passes
    void Clear();

private:
    VkRenderPass CreateRenderPass(const RenderingSignature& config);
    VkFormat ToVulkanFormat(Format format);

    VkDevice m_Device;
    std::unordered_map<RenderingSignature, VkRenderPass> m_Cache;
};
