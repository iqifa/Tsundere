#include "VulkanRenderPassCache.h"
#include <Debug/Debug.h>

VulkanRenderPassCache::VulkanRenderPassCache(VkDevice device)
    : m_Device(device)
{
}

VulkanRenderPassCache::~VulkanRenderPassCache()
{
    Clear();
}

VkFormat VulkanRenderPassCache::ToVulkanFormat(Format format)
{
    switch (format)
    {
    case Format::R8_UNORM:              return VK_FORMAT_R8_UNORM;
    case Format::RGBA8_UNORM:           return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_SRGB:            return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::R16F:                  return VK_FORMAT_R16_SFLOAT;
    case Format::RG16F:                 return VK_FORMAT_R16G16_SFLOAT;
    case Format::RGBA16F:               return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::RGBA32F:               return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::D24_UNORM_S8_UINT:     return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::D32_SFLOAT:            return VK_FORMAT_D32_SFLOAT;
    case Format::D32_SFLOAT_S8_UINT:    return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case Format::R32_UINT:              return VK_FORMAT_R32_UINT;
    case Format::R32_SINT:              return VK_FORMAT_R32_SINT;
    default:
        Error_Core("Unsupported format: {0}", static_cast<uint8_t>(format));
        return VK_FORMAT_UNDEFINED;
    }
}

VkRenderPass VulkanRenderPassCache::CreateRenderPass(const RenderingSignature& config)
{
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> colorRefs;

    // Color attachments
    for (size_t i = 0; i < config.colorFormats.size(); ++i)
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = ToVulkanFormat(config.colorFormats[i]);
        colorAttachment.samples = static_cast<VkSampleCountFlagBits>(config.sampleCount == 4 ? VK_SAMPLE_COUNT_4_BIT : config.sampleCount == 2 ? VK_SAMPLE_COUNT_2_BIT : config.sampleCount == 8 ? VK_SAMPLE_COUNT_8_BIT : VK_SAMPLE_COUNT_1_BIT);
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Default to Load; RDG manages clears
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachments.push_back(colorAttachment);

        VkAttachmentReference colorRef{};
        colorRef.attachment = static_cast<uint32_t>(i);
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs.push_back(colorRef);
    }

    // Depth attachment (optional)
    VkAttachmentReference depthRef{};
    bool hasDepth = config.depthFormat != Format::Unknown;

    if (hasDepth)
    {
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = ToVulkanFormat(config.depthFormat);
        depthAttachment.samples = static_cast<VkSampleCountFlagBits>(config.sampleCount == 4 ? VK_SAMPLE_COUNT_4_BIT : config.sampleCount == 2 ? VK_SAMPLE_COUNT_2_BIT : config.sampleCount == 8 ? VK_SAMPLE_COUNT_8_BIT : VK_SAMPLE_COUNT_1_BIT);
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Default to Load
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachments.push_back(depthAttachment);

        depthRef.attachment = static_cast<uint32_t>(config.colorFormats.size());
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    // Subpass
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.empty() ? nullptr : colorRefs.data();
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    // Subpass dependencies for layout transitions
    std::vector<VkSubpassDependency> dependencies;

    // External -> Subpass 0: Wait for previous frame's writes
    VkSubpassDependency dependency1{};
    dependency1.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency1.dstSubpass = 0;
    dependency1.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency1.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency1.srcAccessMask = 0;
    dependency1.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    dependencies.push_back(dependency1);

    if (hasDepth)
    {
        VkSubpassDependency dependency2{};
        dependency2.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency2.dstSubpass = 0;
        dependency2.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency2.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency2.srcAccessMask = 0;
        dependency2.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependencies.push_back(dependency2);
    }

    // Create render pass
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VkRenderPass renderPass;
    VkResult result = vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &renderPass);

    if (result != VK_SUCCESS)
    {
        Error_Core("Failed to create Vulkan render pass");
        return VK_NULL_HANDLE;
    }

    return renderPass;
}

VkRenderPass VulkanRenderPassCache::GetRenderPass(const RenderingSignature& config)
{
    // Check cache
    auto it = m_Cache.find(config);
    if (it != m_Cache.end())
    {
        return it->second;
    }

    // Create new render pass
    VkRenderPass renderPass = CreateRenderPass(config);
    if (renderPass != VK_NULL_HANDLE)
    {
        m_Cache[config] = renderPass;
    }

    return renderPass;
}

void VulkanRenderPassCache::Clear()
{
    for (auto& [config, renderPass] : m_Cache)
    {
        if (renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_Device, renderPass, nullptr);
        }
    }
    m_Cache.clear();
}
