#pragma once

#include <Platform/RHI/RHIPipeline.h>
#include <vulkan/vulkan_core.h>

class VulkanPipeline : public RHIPipeline
{
public:
    explicit VulkanPipeline(const PipelineDesc& desc);
    ~VulkanPipeline() override;

    void Bind() override;
    void Unbind() override;

    VkPipeline GetPipeline() const { return m_Pipeline; }
    VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }

private:
    PipelineDesc m_Desc;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
};

namespace VulkanPipelineUtil
{
    VkFormat ToVKVertexType(VertexFormat fmt);
    VkFormat ToVkFormat(Format format);
    Format ResolveDepthFormat(Format requested);
    VkSampleCountFlagBits ToVkSampleCount(uint32_t sampleCount);
    VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology);
    VkCullModeFlags ToVKCullMode(CullMode cullmode);
    VkCompareOp ToVkCompareOp(CompareOp op);
    VkBlendFactor ToVkBlendFactor(BlendFactor factor);
}
