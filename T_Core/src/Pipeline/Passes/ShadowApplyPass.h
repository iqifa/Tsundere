#pragma once
#include "Pipeline/Passes/PassCommon.h"

class ShadowApplyPass : public RenderPass
{
public:
	void Init(Ref<RHIFramebuffer> fb) override
	{
		m_Spec = { fb->GetWidth(), fb->GetHeight() };

		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f
		};
		unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };

		m_Shader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/ShadowApply.shader");

		m_QuadVB = RHIBuffer::Create(BufferDesc{ (uint32_t)sizeof(quadVertices), BufferUsage::Vertex, false, quadVertices });
		m_QuadIB = RHIBuffer::Create(BufferDesc{ (uint32_t)(6 * sizeof(unsigned int)), BufferUsage::Index, false, quadIndices });
		m_QuadIndexCount = 6;
		VertexLayout quadLayout;
		quadLayout.stride = 4 * sizeof(float);
		quadLayout.attributes = {
			{ 0, VertexFormat::Float2, 0 },
			{ 1, VertexFormat::Float2, 2 * sizeof(float) },
		};
		PipelineDesc quadDesc;
		quadDesc.shader = m_Shader;
		quadDesc.vertexLayout = quadLayout;
		quadDesc.cullMode = CullMode::None;
		quadDesc.depthTest = false;
		m_QuadPipeline = RHIPipeline::Create(quadDesc);
		CreateOutputTex(m_Spec.Width, m_Spec.Height);
	}

	void Execute(Ref<Scene>, RenderResources& resources) override
	{
		if (!resources.ShadowMask)
			return;

		auto cmd = RHIRenderer::GetCmd();

		// Bind intermediate FBO for output (avoids reading+writing same texture).
		// Fullscreen quad overwrites every pixel, so no clear is needed.
		RenderPassBeginInfo beginInfo;
		cmd->BeginRenderPass(m_OutputFBO, beginInfo);
		cmd->SetDepthTest(false);

		cmd->BindPipeline(m_QuadPipeline);
		cmd->BindVertexBuffer(m_QuadVB);
		cmd->BindIndexBuffer(m_QuadIB);

		// Samplers are at bindings 10 (u_SceneColor) and 11 (u_ShadowMask);
		// in GLSL 420 `layout(binding=N)` makes the sampler read from unit N.
		cmd->BindTexture2D(10, resources.SceneColorTexture);
		cmd->BindTexture2D(11, resources.ShadowMask);

		cmd->DrawIndexed(m_QuadIndexCount);
		cmd->EndRenderPass();

		resources.SceneColorTexture = (unsigned int)m_OutputFBO->GetColorAttachmentID(0);
	}

	// --- RenderGraph path (stage 4) ---
	//
	// Reads scene color + shadow mask, writes a new color target. The graph owns
	// the output texture and the framebuffer, so unlike Execute() this needs no
	// m_OutputTex/m_OutputFBO of its own.
	//
	// shadowMask comes from the legacy compute ShadowPass, so the driver imports
	// it. When there is no mask, don't add this pass at all — just keep using the
	// incoming sceneColor.
	RGTextureHandle AddToGraph(
		RenderGraph& graph,
		RGTextureHandle sceneColor,
		RGTextureHandle shadowMask,
		uint32_t width,
		uint32_t height)
	{
		RDGTextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = Format::RGBA8_UNORM;
		desc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

		RGTextureHandle output = graph.CreateTexture(desc, "ShadowApply.Output");

		graph.AddPass(
			"ShadowApply",
			[sceneColor, shadowMask, output](RenderGraphPassBuilder& builder)
			{
				builder.ReadTexture(sceneColor, RGAccess::ReadSRV);
				builder.ReadTexture(shadowMask, RGAccess::ReadSRV);
				builder.SetColorAttachment(0, output, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 1.0f });
			},
			[this, sceneColor, shadowMask](RHICommandBuffer& cmd, RenderGraphResources& resources)
			{
				RHITexture2D* color = resources.GetTexture(sceneColor);
				RHITexture2D* mask = resources.GetTexture(shadowMask);
				if (!color || !mask)
					return;

				// Fullscreen resolve — depth testing would reject the quad against
				// whatever depth the previous pass left bound.
				// Fullscreen resolve — depth testing would reject the quad against
				// whatever depth the previous pass left bound.
				cmd.SetDepthTest(false);

				cmd.BindPipeline(m_QuadPipeline);
				cmd.BindVertexBuffer(m_QuadVB);
				cmd.BindIndexBuffer(m_QuadIB);

				cmd.BindTexture2D(10, color->GetNativeID());
				cmd.BindTexture2D(11, mask->GetNativeID());

				cmd.DrawIndexed(m_QuadIndexCount);
			});

		return output;
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
		CreateOutputTex(w, h);
	}

private:
	void CreateOutputTex(unsigned int w, unsigned int h)
	{
		m_OutputFBO = RHIFramebuffer::Create(FramebufferDesc{
			w, h, { { Format::RGBA8_UNORM, 1 } }, false, 1 });
	}

	FrameBufferSpecification m_Spec;
	Ref<RHIShader> m_Shader;
	Ref<RHIBuffer> m_QuadVB;
	Ref<RHIBuffer> m_QuadIB;
	Ref<RHIPipeline> m_QuadPipeline;
	unsigned int m_QuadIndexCount = 0;
	Ref<RHIFramebuffer> m_OutputFBO;
};

