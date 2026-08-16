#pragma once
#include "Pipeline/Passes/PassCommon.h"

class TAAPass : public RenderPass
{
public:
	bool Enabled = true;

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

		m_Shader = CreatePtr<GLShader>("D:/Code/C++/Tsundere/res/shaders/TAA.shader");
	}

	void Execute(Ref<Scene>, RenderResources& resources) override
	{

		if (!Enabled) return;

		if (!m_HistoryFBOs[0])
		{
			m_HistoryFBOs[0] = CreatePtr<FrameBuffer>(m_Spec);
			m_HistoryFBOs[1] = CreatePtr<FrameBuffer>(m_Spec);
		}
		if (!m_PrevDepthFBO)
			m_PrevDepthFBO = CreatePtr<FrameBuffer>(m_Spec);

		int nextIdx = (m_CurrentIdx + 1) % 2;

		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 currentViewProj = proj * view;

		m_HistoryFBOs[nextIdx]->Bind();
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if (m_Shader)
		{
			m_Shader->Bind();




			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, resources.SceneColorTexture);
			m_Shader->SetUniform1i("u_CurrentColor", 0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, m_HistoryFBOs[m_CurrentIdx]->GetClolorAttachmentRenderID());
			m_Shader->SetUniform1i("u_HistoryColor", 1);

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, resources.VelocityTexture);
			m_Shader->SetUniform1i("u_VelocityTex", 2);

			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, resources.DepthTexture);
			m_Shader->SetUniform1i("u_DepthTex", 3);

			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D, m_PrevDepthFBO->GetDepthAttachmentRenderID());
			m_Shader->SetUniform1i("u_HistoryDepthTex", 4);

			m_Shader->SetUniformMat4f("u_InverseViewProj", glm::inverse(currentViewProj));
			m_Shader->SetUniformMat4f("u_PrevViewProj", m_PrevViewProj);

			Renderer renderer;
			renderer.DrawElement(*m_QuadVA, *m_QuadIB, *m_Shader);
		}

		m_HistoryFBOs[nextIdx]->UnBind();

		if (resources.SourceFBO)
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, resources.SourceFBO);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_PrevDepthFBO->GetFrameID());
			glBlitFramebuffer(0, 0, m_Spec.Width, m_Spec.Height,
				0, 0, m_Spec.Width, m_Spec.Height,
				GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		}

		m_PrevViewProj = currentViewProj;
		m_CurrentIdx = nextIdx;

		resources.SceneColorTexture = m_HistoryFBOs[m_CurrentIdx]->GetClolorAttachmentRenderID();
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
		if (m_HistoryFBOs[0]) m_HistoryFBOs[0]->Rsetsize({ w, h });
		if (m_HistoryFBOs[1]) m_HistoryFBOs[1]->Rsetsize({ w, h });
		if (m_PrevDepthFBO)  m_PrevDepthFBO->Rsetsize({ w, h });

		// Graph-path history is owned here too, so it resizes with the viewport.
		if (m_GraphHistory)   m_GraphHistory->Resize(w, h);
		if (m_GraphPrevDepth) m_GraphPrevDepth->Resize(w, h);
	}

	// --- RenderGraph path ---------------------------------------------------
	// TAA is temporal, so its history outlives any single frame's graph. A
	// transient graph resource cannot express that: it has no contents when
	// execution begins. So the history lives here, persistently, and is
	// imported into the graph each time the graph is built.
	//
	// The legacy path ping-pongs between two history FBOs. That does not
	// translate directly, because a pass's render target is fixed when the
	// graph is built, and this graph is built once and replayed. Instead:
	//   Resolve      reads history, writes a graph-owned output
	//   StoreHistory copies that output back into history for the next frame
	// One full-res copy per frame, in exchange for a graph that is not rebuilt
	// every frame. A resource pool would allow the ping-pong form later.
	RGTextureHandle AddToGraph(
		RenderGraph& graph,
		RGTextureHandle sceneColor,
		RGTextureHandle velocity,
		RGTextureHandle depth,
		uint32_t width,
		uint32_t height)
	{
		EnsureGraphHistory(width, height);

		RDGTextureDesc colorDesc;
		colorDesc.width  = width;
		colorDesc.height = height;
		colorDesc.format = Format::RGBA8_UNORM;
		colorDesc.usage  = TextureUsage::ColorAttachment | TextureUsage::Sampled;

		RDGTextureDesc depthDesc;
		depthDesc.width  = width;
		depthDesc.height = height;
		depthDesc.format = Format::D24_UNORM_S8_UINT;
		depthDesc.usage  = TextureUsage::DepthStencil | TextureUsage::Sampled;

		RGTextureHandle history =
			graph.ImportTexture(m_GraphHistory.get(), colorDesc, "TAA.History");
		RGTextureHandle prevDepth =
			graph.ImportTexture(m_GraphPrevDepth.get(), depthDesc, "TAA.PrevDepth");

		RGTextureHandle output = graph.CreateTexture(colorDesc, "TAA.Output");

		graph.AddPass(
			"TAA.Resolve",
			[sceneColor, velocity, depth, history, prevDepth, output](RenderGraphPassBuilder& builder)
			{
				builder.ReadTexture(sceneColor);
				builder.ReadTexture(velocity);
				builder.ReadTexture(depth);
				builder.ReadTexture(history);
				builder.ReadTexture(prevDepth);

				builder.SetColorAttachment(0, output, RGLoadOp::Clear,
					{ 0.1f, 0.1f, 0.1f, 1.0f });
			},
			[this, sceneColor, velocity, depth, history, prevDepth](
				RHICommandBuffer& cmd, RenderGraphResources& resources)
			{
				if (!m_Shader)
					return;

				mat4 view = currentcamera->GetViewFront();
				mat4 proj = currentcamera->GetProj();
				mat4 currentViewProj = proj * view;

				auto bindTex = [&](uint32_t unit, RGTextureHandle handle, const char* uniform)
				{
					RHITexture2D* tex = resources.GetTexture(handle);
					glActiveTexture(GL_TEXTURE0 + unit);
					glBindTexture(GL_TEXTURE_2D,
						tex ? (unsigned int)tex->GetNativeID() : 0);
					m_Shader->SetUniform1i(uniform, (int)unit);
				};

				m_Shader->Bind();

				bindTex(0, sceneColor, "u_CurrentColor");
				bindTex(1, history,    "u_HistoryColor");
				bindTex(2, velocity,   "u_VelocityTex");
				bindTex(3, depth,      "u_DepthTex");
				bindTex(4, prevDepth,  "u_HistoryDepthTex");

				m_Shader->SetUniformMat4f("u_InverseViewProj", glm::inverse(currentViewProj));
				m_Shader->SetUniformMat4f("u_PrevViewProj", m_PrevViewProj);

				// A fullscreen resolve must not be depth-rejected by the target.
				glDisable(GL_DEPTH_TEST);

				Renderer renderer;
				renderer.DrawElement(*m_QuadVA, *m_QuadIB, *m_Shader);

				glEnable(GL_DEPTH_TEST);

				// Advanced at execute time, not build time: the graph is built
				// once and replayed, so per-frame state must update in here.
				m_PrevViewProj = currentViewProj;
			});

		// Copy passes declare no attachments, so the graph binds no framebuffer
		// and simply runs the lambda. glCopyImageSubData needs no FBO at all.
		graph.AddPass(
			"TAA.StoreHistory",
			[output, history](RenderGraphPassBuilder& builder)
			{
				builder.ReadTexture(output, RGAccess::CopySrc);
				builder.WriteTexture(history, RGAccess::CopyDst);
			},
			[output, history, width, height](
				RHICommandBuffer& cmd, RenderGraphResources& resources)
			{
				RHITexture2D* src = resources.GetTexture(output);
				RHITexture2D* dst = resources.GetTexture(history);
				if (!src || !dst)
					return;

				glCopyImageSubData(
					(unsigned int)src->GetNativeID(), GL_TEXTURE_2D, 0, 0, 0, 0,
					(unsigned int)dst->GetNativeID(), GL_TEXTURE_2D, 0, 0, 0, 0,
					(int)width, (int)height, 1);
			});

		graph.AddPass(
			"TAA.StorePrevDepth",
			[depth, prevDepth](RenderGraphPassBuilder& builder)
			{
				builder.ReadTexture(depth, RGAccess::CopySrc);
				builder.WriteTexture(prevDepth, RGAccess::CopyDst);
			},
			[depth, prevDepth, width, height](
				RHICommandBuffer& cmd, RenderGraphResources& resources)
			{
				RHITexture2D* src = resources.GetTexture(depth);
				RHITexture2D* dst = resources.GetTexture(prevDepth);
				if (!src || !dst)
					return;

				glCopyImageSubData(
					(unsigned int)src->GetNativeID(), GL_TEXTURE_2D, 0, 0, 0, 0,
					(unsigned int)dst->GetNativeID(), GL_TEXTURE_2D, 0, 0, 0, 0,
					(int)width, (int)height, 1);
			});

		return output;
	}

private:
	// Persistent, graph-independent history. Created on first graph build and
	// resized with the viewport; imported (never created) by the graph.
	void EnsureGraphHistory(uint32_t width, uint32_t height)
	{
		if (!m_GraphHistory)
		{
			Texture2DDesc desc;
			desc.width  = width;
			desc.height = height;
			desc.format = Format::RGBA8_UNORM;
			m_GraphHistory = RHITexture2D::Create(desc);
		}
		else if (m_GraphHistory->GetWidth() != width || m_GraphHistory->GetHeight() != height)
		{
			m_GraphHistory->Resize(width, height);
		}

		if (!m_GraphPrevDepth)
		{
			Texture2DDesc desc;
			desc.width  = width;
			desc.height = height;
			// Must match Geometry.Depth: glCopyImageSubData requires compatible formats.
			desc.format = Format::D24_UNORM_S8_UINT;
			m_GraphPrevDepth = RHITexture2D::Create(desc);
		}
		else if (m_GraphPrevDepth->GetWidth() != width || m_GraphPrevDepth->GetHeight() != height)
		{
			m_GraphPrevDepth->Resize(width, height);
		}
	}

	Ptr<FrameBuffer> m_HistoryFBOs[2];
	Ptr<FrameBuffer> m_PrevDepthFBO;
	Ptr<GLShader> m_Shader;
	Ptr<VertexArray> m_QuadVA;
	Ptr<VertexBuffer> m_QuadVB;
	Ptr<IndexBuffer> m_QuadIB;

	// RenderGraph path only — the legacy path keeps using m_HistoryFBOs.
	Ref<RHITexture2D> m_GraphHistory;
	Ref<RHITexture2D> m_GraphPrevDepth;

	int m_CurrentIdx = 0;
	mat4 m_PrevViewProj = mat4(1.0f);
	FrameBufferSpecification m_Spec;
};

