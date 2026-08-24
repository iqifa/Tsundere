#pragma once
#include "Pipeline/Passes/PassCommon.h"

class TAAPass : public RenderPass
{
public:
	bool Enabled = true;

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

		m_Shader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/TAA.shader");

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

		// TAA's per-pass UBO: 2 mat4s. std140 layout packs each mat4 at
		// 16-byte alignment, 64 bytes total.
		m_UBO = RHIBuffer::Create(BufferDesc{ sizeof(TAAUBO), BufferUsage::Uniform, true, nullptr });
	}

	void Execute(Ref<Scene>, RenderResources& resources) override
	{

		if (!Enabled) return;

		if (!m_HistoryFBOs[0])
		{
			m_HistoryFBOs[0] = RHIFramebuffer::Create(FramebufferDesc{ m_Spec.Width, m_Spec.Height, { { Format::RGBA8_UNORM, 1 } }, true, 1 });
			m_HistoryFBOs[1] = RHIFramebuffer::Create(FramebufferDesc{ m_Spec.Width, m_Spec.Height, { { Format::RGBA8_UNORM, 1 } }, true, 1 });
		}
		if (!m_PrevDepthFBO)
			m_PrevDepthFBO = RHIFramebuffer::Create(FramebufferDesc{ m_Spec.Width, m_Spec.Height, { { Format::RGBA8_UNORM, 1 } }, true, 1 });

		int nextIdx = (m_CurrentIdx + 1) % 2;

		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 currentViewProj = proj * view;

		auto cmd = RHIRenderer::GetCmd();

		RenderPassBeginInfo beginInfo;
		beginInfo.colorClears = { { true, { 0.1f, 0.1f, 0.1f, 1.0f } } };
		beginInfo.clearDepth      = true;
		beginInfo.depthClearValue = 1.0f;
		cmd->BeginRenderPass(m_HistoryFBOs[nextIdx], beginInfo);

		if (m_Shader)
		{
			// Fill the per-pass UBO and push it. The struct mirrors
			// PerPass_TAA in TAA.shader (std140 layout).
			TAAUBO ubo;
			ubo.u_InverseViewProj = glm::inverse(currentViewProj);
			ubo.u_PrevViewProj    = m_PrevViewProj;
			m_UBO->Upload(&ubo, sizeof(ubo));

			// Bind samplers. With GLSL 420's `layout(binding=N)` qualifier,
			// sampler N reads from texture unit N. The texture unit we hand
			// the descriptor set must match the binding. Phase 3 will fold
			// these into the descriptor set too; for now keep cmd->BindTexture2D.
			RHITexture2D* historyCol = m_HistoryFBOs[m_CurrentIdx]->GetColorAttachment(0);
			RHITexture2D* historyDep = m_PrevDepthFBO->GetDepthAttachment();
			cmd->BindTexture2D(10, resources.SceneColorTexture);
			if (historyCol) cmd->BindTexture2D(11, historyCol->GetNativeID());
			cmd->BindTexture2D(12, resources.VelocityTexture);
			cmd->BindTexture2D(13, resources.DepthTexture);
			if (historyDep) cmd->BindTexture2D(14, historyDep->GetNativeID());
			// Bind the UBO at binding 0 — the descriptor set owns this
			// binding from here on.
			m_DescriptorSet->Reset();
			m_DescriptorSet->BindUniformBuffer(0, m_UBO);
			m_DescriptorSet->Apply(0);

			cmd->BindPipeline(m_QuadPipeline);
			cmd->BindVertexBuffer(m_QuadVB);
			cmd->BindIndexBuffer(m_QuadIB);

			cmd->DrawIndexed(m_QuadIndexCount);
		}

		cmd->EndRenderPass();

		if (resources.SourceFBO)
		{
			cmd->BlitDepth(resources.SourceFBO, m_PrevDepthFBO);
		}

		m_PrevViewProj = currentViewProj;
		m_CurrentIdx = nextIdx;

		resources.SceneColorTexture = (unsigned int)m_HistoryFBOs[m_CurrentIdx]->GetColorAttachmentID(0);
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
		if (m_HistoryFBOs[0]) m_HistoryFBOs[0]->Resize(w, h);
		if (m_HistoryFBOs[1]) m_HistoryFBOs[1]->Resize(w, h);
		if (m_PrevDepthFBO)  m_PrevDepthFBO->Resize(w, h);

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

				// A fullscreen resolve must not be depth-rejected by the target.
				cmd.SetDepthTest(false);

				// match the legacy path: UBO at binding 0, samplers at 10..14.
				// The shader parser no longer exposes u_InverseViewProj /
				// u_PrevViewProj as standalone uniforms — they live in PerPass_TAA
				// now, so the only way to feed them is via the UBO + descriptor set.
				TAAUBO ubo;
				mat4 view = currentcamera->GetViewFront();
				mat4 proj = currentcamera->GetProj();
				mat4 currentViewProj = proj * view;
				ubo.u_InverseViewProj = glm::inverse(currentViewProj);
				ubo.u_PrevViewProj    = m_PrevViewProj;
				m_UBO->Upload(&ubo, sizeof(ubo));

				m_DescriptorSet->Reset();
				m_DescriptorSet->BindUniformBuffer(0, m_UBO);
				m_DescriptorSet->Apply(0);

				auto bindTex = [&](uint32_t unit, RGTextureHandle handle)
				{
					RHITexture2D* tex = resources.GetTexture(handle);
					cmd.BindTexture2D(unit, tex ? tex->GetNativeID() : 0);
				};

				bindTex(10, sceneColor);
				bindTex(11, history);
				bindTex(12, velocity);
				bindTex(13, depth);
				bindTex(14, prevDepth);

				cmd.BindPipeline(m_QuadPipeline);
				cmd.BindVertexBuffer(m_QuadVB);
				cmd.BindIndexBuffer(m_QuadIB);

				cmd.DrawIndexed(m_QuadIndexCount);

				cmd.SetDepthTest(true);

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

				cmd.CopyTexture(src, dst);
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

				cmd.CopyTexture(src, dst);
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

	Ref<RHIFramebuffer> m_HistoryFBOs[2];
	Ref<RHIFramebuffer> m_PrevDepthFBO;
	Ref<RHIShader> m_Shader;
	Ref<RHIBuffer> m_QuadVB;
	Ref<RHIBuffer> m_QuadIB;
	Ref<RHIPipeline> m_QuadPipeline;
	unsigned int m_QuadIndexCount = 0;

	// Per-pass UBO (matches PerPass_TAA in TAA.shader). std140 packs each
	// mat4 at 16-byte alignment, so the struct must use glm::mat4 (already
	// 16-byte aligned) and not any custom packing.
	struct TAAUBO
	{
		glm::mat4 u_InverseViewProj;   // offset  0
		glm::mat4 u_PrevViewProj;      // offset 64
	};
	Ref<RHIBuffer>       m_UBO;
	Ref<RHIDescriptorSet> m_DescriptorSet = RHIDescriptorSet::Create();

	// RenderGraph path only — the legacy path keeps using m_HistoryFBOs.
	Ref<RHITexture2D> m_GraphHistory;
	Ref<RHITexture2D> m_GraphPrevDepth;

	int m_CurrentIdx = 0;
	mat4 m_PrevViewProj = mat4(1.0f);
	FrameBufferSpecification m_Spec;
};

