#pragma once
#include "Pipeline/RenderGraphPass.h"

// Separate conversion and display passes preserve the selected storage format:
// a single-channel texture is sampled again before channel visualization.
class ViewportPreviewPass final : public RenderGraphPass
{
public:
    ViewportPreviewPass(const char* name, Ref<RHIShader> shader,
        std::vector<std::string> inputs, Format format, glm::vec4 settings,
        uint32_t width, uint32_t height)
        : m_Name(name), m_Shader(shader), m_Inputs(std::move(inputs)), m_Format(format),
          m_Settings(settings), m_Width(width), m_Height(height) {}
    const char* GetName() const override { return m_Name; }
    const std::string& SourceName() const { return m_SourceName; }
    Format SourceFormat() const { return m_SourceFormat; }
    void Setup(RenderGraphBuilder& builder) override
    {
        for (const auto& name : m_Inputs)
        {
            if (name.empty()) continue;
            m_Input = builder.ReadTexture(name);
            if (m_Input.id != InvalidResourceId) { m_SourceName = name; break; }
        }
        if (m_Input.id == InvalidResourceId) return;
        RDGTextureDesc target;
        target.width = m_Width; target.height = m_Height; target.format = m_Format;
        target.minFilter = target.magFilter = FilterMode::Nearest;
        target.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
        auto output = builder.CreateTexture("Color", target);
        builder.SetColorOutput(0, output, RGLoadOp::DontCare);
        builder.Export(output);
        m_Descriptors = RHIDescriptorSet::Create();
        m_Descriptors->BindTexture(0, static_cast<RHITexture2D*>(nullptr), 0);
        PipelineDesc desc;
        desc.depthTest = false; desc.depthWrite = false; desc.cullMode = CullMode::None;
        desc.pushConstantSize = sizeof(glm::vec4); desc.descriptorSets = {m_Descriptors};
        m_Pipeline = builder.CreatePipeline(m_Shader, {}, &desc);
    }
    void Execute(RenderGraphContext& context) override
    {
        if (!m_Pipeline) return;
        auto* input = context.GetTexture(m_Input);
        if (!input) return;
        m_SourceFormat = input->GetFormat();
        m_Descriptors->Reset(); m_Descriptors->BindTexture(0, input, 0); m_Descriptors->Apply(0);
        auto& cmd = context.GetCmd();
        cmd.BindPipeline(m_Pipeline); cmd.BindDescriptorSet(m_Descriptors, 0);
        cmd.PushConstants(&m_Settings, sizeof(m_Settings)); cmd.Draw(3);
    }
private:
    const char* m_Name;
    Ref<RHIShader> m_Shader;
    std::vector<std::string> m_Inputs;
    std::string m_SourceName;
    Format m_Format, m_SourceFormat = Format::Unknown;
    glm::vec4 m_Settings;
    uint32_t m_Width, m_Height;
    RGTextureHandle m_Input;
    Ref<RHIDescriptorSet> m_Descriptors;
    Ref<RHIPipeline> m_Pipeline;
};
