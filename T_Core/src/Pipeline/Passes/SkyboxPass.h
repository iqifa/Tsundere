#pragma once
#include "Pipeline/Passes/OpaqueSceneInputs.h"
#include "Pipeline/Passes/PassCommon.h"

// Always inserted after opaque/TAA and before transparency. Sky radiance is
// never stored in TAA history; scene alpha carries resolved opaque coverage.
class SkyboxPass final : public RenderGraphPass
{
public:
    explicit SkyboxPass(Ref<RGFrameData> frame) : m_Frame(std::move(frame)) {}
    const char* GetName() const override { return "Skybox"; }
    void Setup(RenderGraphBuilder& builder) override
    {
        m_Input = builder.ReadTexture("TAA.SceneColor");
        if (m_Input.id != InvalidResourceId) {
            builder.ReadTexture("TAA.SceneColor", RGAccess::CopySrc);
            m_Depth = builder.ReadTexture("TAA.Depth");
        }
        else {
            const auto opaque = ReadOpaqueSceneInputs(builder);
            if (!opaque.IsValid()) return;
            m_Input = builder.ReadTexture(opaque.colorName, RGAccess::CopySrc);
            m_Depth = opaque.depth;
        }
        if (m_Depth.id == InvalidResourceId) return;
        auto desc = builder.GetTextureDesc(m_Input);
        desc.sampleCount = 1;
        desc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled |
                     TextureUsage::TransferSrc | TextureUsage::TransferDst;
        m_Output = builder.CreateTexture("SceneColor",desc);
        builder.SetColorOutput(0,m_Output,RGLoadOp::Load);
        builder.WriteTexture(m_Output,RGAccess::CopyDst);
        builder.DescribeTextureCopy(m_Input,m_Output);
        builder.Export(m_Output);
        if (!currentcamera || !currentcamera->skybox) return;
        m_Sky = currentcamera->skybox->m_Cmp;
        m_Shader = RHIShader::Create("res/shaders/SkyboxComposite.shader");
        m_Uniform = RHIBuffer::Create({sizeof(glm::mat4),BufferUsage::Uniform,true,nullptr});
        m_Set = RHIDescriptorSet::Create();
        m_Set->BindUniformBuffer(0,m_Uniform);
        m_Set->BindTexture(10,static_cast<RHITexture2D*>(nullptr),10);
        m_Set->BindTexture(11,static_cast<RHITexture2D*>(nullptr),11);
        m_Set->BindCubeMap(12,m_Sky,12);
        PipelineDesc pipeline;
        pipeline.depthTest = false;
        pipeline.depthWrite = false;
        pipeline.cullMode = CullMode::None;
        pipeline.descriptorSets = {m_Set};
        m_Pipeline = builder.CreatePipeline(m_Shader,{},&pipeline);
    }
    void BeforeRender(RenderGraphContext& context) override
    {
        if (m_Output.id != InvalidResourceId)
            context.GetCmd().CopyTexture(context.GetTexture(m_Input),context.GetTexture(m_Output));
    }
    void Execute(RenderGraphContext& context) override
    {
        if (!m_Pipeline) return;
        // The camera's stable projection and rotation only: no jitter or parallax.
        const auto projection = m_Frame->Camera.ViewProj * glm::inverse(m_Frame->Camera.View);
        const auto inverse = glm::inverse(projection * glm::mat4(glm::mat3(m_Frame->Camera.View)));
        m_Uniform->Upload(&inverse,sizeof(inverse));
        m_Set->Reset();
        m_Set->BindUniformBuffer(0,m_Uniform);
        m_Set->BindTexture(10,context.GetTexture(m_Input),10);
        m_Set->BindTexture(11,context.GetTexture(m_Depth),11);
        m_Set->BindCubeMap(12,m_Sky,12);
        m_Set->Apply(0);
        auto& cmd = context.GetCmd();
        cmd.BindPipeline(m_Pipeline);
        cmd.BindDescriptorSet(m_Set,0);
        cmd.Draw(3);
    }
private:
    Ref<RGFrameData> m_Frame;
    RGTextureHandle m_Input, m_Depth, m_Output;
    Ref<RHITextureCube> m_Sky;
    Ref<RHIShader> m_Shader;
    Ref<RHIBuffer> m_Uniform;
    Ref<RHIDescriptorSet> m_Set;
    Ref<RHIPipeline> m_Pipeline;
};
