#pragma once

#include "Pipeline/Passes/ForwardMeshRenderer.h"
#include "Pipeline/Passes/OpaqueSceneInputs.h"
#include "Pipeline/RenderPassRegistry.h"

class TransparentPass : public RenderGraphPass
{
public:
	TransparentPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData)
	{
		PipelineDesc desc;
		desc.cullMode = CullMode::Back;
		desc.depthTest = true;
		desc.depthWrite = false;
		desc.srcBlend = BlendFactor::SrcAlpha;
		desc.dstBlend = BlendFactor::OneMinusSrcAlpha;
		desc.pushConstantSize = sizeof(DrawPushConstants);
		desc.renderingSignature.colorFormats = { Format::RGBA8_UNORM };
		desc.renderingSignature.depthFormat = Format::D24_UNORM_S8_UINT;
		m_MeshRenderer.ConfigurePipeline(std::move(desc));
	}

	const char* GetName() const override { return "Transparent"; }

	void SetViewportSize(uint32_t width, uint32_t height) override
	{
		m_Width = width;
		m_Height = height;
	}

	void Setup(RenderGraphBuilder& builder) override
	{
		m_SceneColor = {};
		const auto opaque = ReadOpaqueSceneInputs(builder);
		if (!opaque.IsValid())
			return;

		m_OpaqueColor = builder.ReadTexture(opaque.colorName, RGAccess::CopySrc);
		m_OpaqueDepth = builder.ReadTexture(opaque.depthName, RGAccess::CopySrc);
		m_ShadowMap = builder.ReadTexture("ShadowMap.Depth");

		RDGTextureDesc color;
		color.width = m_Width;
		color.height = m_Height;
		color.format = Format::RGBA8_UNORM;
		color.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferDst;
		m_SceneColor = builder.CreateTexture("SceneColor", color);
		builder.WriteTexture(m_SceneColor, RGAccess::CopyDst);
		builder.SetColorOutput(0, m_SceneColor, RGLoadOp::Load);
		builder.SetDepthOutput(m_OpaqueDepth, RGLoadOp::Load, 1.0f, true);

		// Sample a copy so OpenGL can keep the original bound for depth testing.
		RDGTextureDesc depth = color;
		depth.format = Format::D24_UNORM_S8_UINT;
		depth.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
		depth.minFilter = FilterMode::Nearest;
		depth.magFilter = FilterMode::Nearest;
		m_OpaqueDepthSample = builder.CreateTexture("OpaqueDepth", depth);
		builder.WriteTexture(m_OpaqueDepthSample, RGAccess::CopyDst);
		builder.ReadTexture("OpaqueDepth");
		builder.Export(m_SceneColor);
	}

	void BeforeRender(RenderGraphContext& context) override
	{
		if (m_SceneColor.id == InvalidResourceId)
			return;
		auto& cmd = context.GetCmd();
		cmd.CopyTexture(context.GetTexture(m_OpaqueColor), context.GetTexture(m_SceneColor));
		cmd.CopyTexture(context.GetTexture(m_OpaqueDepth), context.GetTexture(m_OpaqueDepthSample));
	}

	void Execute(RenderGraphContext& context) override
	{
		if (m_SceneColor.id == InvalidResourceId)
			return;

		struct DrawItem
		{
			Ref<RHIMesh> mesh;
			Ref<Engine::Material> material;
			glm::mat4 model;
			glm::mat4 previous;
			float depth;
		};
		std::vector<DrawItem> items;
		const auto& view = m_FrameData->Camera.View;
		ForwardMeshRenderer::VisitReadyMeshes(*m_Scene, *m_FrameData,
			[&](const Ref<RHIMesh>& mesh, const Ref<Engine::Material>& material,
				const glm::mat4& model, const glm::mat4& previous)
			{
				if (material->GetShader()->GetReflection().Transparent)
					items.push_back({ mesh, material, model, previous, -(view * model[3]).z });
			});
		if (items.empty())
			return;

		std::stable_sort(items.begin(), items.end(),
			[](const DrawItem& a, const DrawItem& b) { return a.depth > b.depth; });
		auto& cmd = context.GetCmd();
		const ForwardMeshRenderer::Textures textures{
			m_ShadowMap.id != InvalidResourceId ? context.GetTexture(m_ShadowMap) : nullptr,
			context.GetTexture(m_OpaqueColor),
			context.GetTexture(m_OpaqueDepthSample)
		};
		m_MeshRenderer.UploadFrame(*m_Scene, *m_FrameData, textures.shadow);
		for (const auto& item : items)
			m_MeshRenderer.Draw(cmd, *item.mesh, item.material, item.model, item.previous, textures);
	}

private:
	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	ForwardMeshRenderer m_MeshRenderer;
	RGTextureHandle m_ShadowMap;
	RGTextureHandle m_OpaqueColor;
	RGTextureHandle m_OpaqueDepth;
	RGTextureHandle m_OpaqueDepthSample;
	RGTextureHandle m_SceneColor;
	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;
};

REGISTER_RENDER_PASS(TransparentPass, "Transparent", "Base", 15, true)
