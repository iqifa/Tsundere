#pragma once
#include "Pipeline/Passes/PassCommon.h"

class ShadowApplyPass : public RenderPass
{
public:
	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();

		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f
		};
		unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };

		m_QuadVA = CreatePtr<VertexArray>(4);
		m_QuadVB = CreatePtr<VertexBuffer>(quadVertices, sizeof(quadVertices));
		m_QuadIB = CreatePtr<IndexBuffer>(quadIndices, 6);
		VertexBufferLayout quadLayout;
		quadLayout.Push<float>(2);
		quadLayout.Push<float>(2);
		m_QuadVA->AddBuffer(*m_QuadVB, quadLayout);

		m_Shader = CreatePtr<GLShader>("D:/Code/C++/Tsundere/res/shaders/ShadowApply.shader");
		CreateOutputTex(m_Spec.Width, m_Spec.Height);
	}

	void Execute(Ref<Scene>, RenderResources& resources) override
	{
		if (!resources.ShadowMask)
			return;

		// Bind intermediate FBO for output (avoids reading+writing same texture)
		glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
		glViewport(0, 0, m_Spec.Width, m_Spec.Height);

		m_Shader->Bind();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, resources.SceneColorTexture);
		m_Shader->SetUniform1i("u_SceneColor", 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, resources.ShadowMask);
		m_Shader->SetUniform1i("u_ShadowMask", 1);

		Renderer renderer;
		renderer.DrawElement(*m_QuadVA, *m_QuadIB, *m_Shader);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		resources.SceneColorTexture = m_OutputTex;
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

				m_Shader->Bind();

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, (GLuint)color->GetNativeID());
				m_Shader->SetUniform1i("u_SceneColor", 0);

				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, (GLuint)mask->GetNativeID());
				m_Shader->SetUniform1i("u_ShadowMask", 1);

				// Fullscreen resolve — depth testing would reject the quad against
				// whatever depth the previous pass left bound.
				glDisable(GL_DEPTH_TEST);

				Renderer renderer;
				renderer.DrawElement(*m_QuadVA, *m_QuadIB, *m_Shader);
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
		if (m_OutputTex)
			glDeleteTextures(1, &m_OutputTex);
		if (m_OutputFBO)
			glDeleteFramebuffers(1, &m_OutputFBO);

		glGenTextures(1, &m_OutputTex);
		glBindTexture(GL_TEXTURE_2D, m_OutputTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenFramebuffers(1, &m_OutputFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTex, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	FrameBufferSpecification m_Spec;
	Ptr<GLShader> m_Shader;
	Ptr<VertexArray> m_QuadVA;
	Ptr<VertexBuffer> m_QuadVB;
	Ptr<IndexBuffer> m_QuadIB;
	unsigned int m_OutputTex = 0;
	unsigned int m_OutputFBO = 0;
};

