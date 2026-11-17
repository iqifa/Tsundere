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
    VkPipelineBindPoint GetBindPoint() const { return m_BindPoint; }
    VkShaderStageFlags GetPushConstantStages() const { return m_PushConstantStages; }

private:
    bool CreatePipelineLayout(VkDevice device, VkShaderStageFlags pushStages);
    void CreateComputePipeline();

    PipelineDesc m_Desc;
    VkPipelineBindPoint m_BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkShaderStageFlags m_PushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
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
