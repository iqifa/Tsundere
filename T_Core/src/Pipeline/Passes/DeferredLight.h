#pragma once
#include<Pipeline/Passes/PassCommon.h>
#include "Pipeline/RenderPassRegistry.h"
#include "Pipeline/Passes/ForwardMeshRenderer.h"
#include <array>

class DeferredLightPass :public RenderGraphPass {
public:
	DeferredLightPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData) {

		constexpr uint32_t maxDrawObjects = 16;
		m_DeferredLightingUBO = RHIBuffer::Create(BufferDesc{ sizeof(DeferredLightDrawUBO) * maxDrawObjects, BufferUsage::Uniform, true, nullptr });
		constexpr uint32_t maxLightCapicity = 128;
		m_LightsData = RHIBuffer::Create(BufferDesc{ maxLightCapicity * sizeof(GPULight), BufferUsage::Storage, true, nullptr });
	
		m_DeferredLightShader = RHIShader::Create("res/shaders/DeferredLight.shader");
		ValidateShaderLayout();

		unsigned char white[4] = { 255, 255, 255, 255 };
		m_DefaultTex = RHITexture2D::Create({ 1, 1, Format::RGBA8_UNORM, FilterMode::Linear, FilterMode::Linear,
											 WrapMode::ClampToEdge, WrapMode::ClampToEdge, false, white });
		m_DeferredDescriptorSet = RHIDescriptorSet::Create();

		if (m_DeferredDescriptorSet)
		{
			m_DeferredDescriptorSet->BindUniformBuffer(0, m_DeferredLightingUBO, sizeof(DeferredLightDrawUBO));
			m_DeferredDescriptorSet->BindStorageBuffer(1, m_LightsData);			m_DeferredDescriptorSet->BindTexture(10, m_DefaultTex, 10);
			m_DeferredDescriptorSet->BindTexture(11, m_DefaultTex, 11);
			m_DeferredDescriptorSet->BindTexture(12, m_DefaultTex, 12);
			m_DeferredDescriptorSet->BindTexture(13, m_DefaultTex, 13);
			m_DeferredDescriptorSet->BindTexture(14, m_DefaultTex, 14);
			m_DeferredDescriptorSet->BindTexture(15, m_DefaultTex, 15);


			m_DeferredDescriptorSet->MarkBindingAsDynamic(0);
		}
		else {
			Error_Core("[{0} Pass]:m_GbufferDescriptSet is Null", GetName());
		}


		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f
		};
		unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };

		ScreenVB = RHIBuffer::Create({ 4 * 4 * (uint32_t)sizeof(float),BufferUsage::Vertex,false,quadVertices });
		ScreenIB = RHIBuffer::Create({ 6 * (uint32_t)sizeof(float),BufferUsage::Index,false,quadIndices });


		VertexLayout ScreenVertexLayout;
		ScreenVertexLayout.stride = 4 * sizeof(float);
		ScreenVertexLayout.attributes = {
			{0,VertexFormat::Float2,0,0},
			{1,VertexFormat::Float2,2 * sizeof(float),0},
		};


		PipelineDesc pipeDesc;
		pipeDesc.shader = m_DeferredLightShader;
		pipeDesc.vertexLayout = ScreenVertexLayout;
		pipeDesc.topology = PrimitiveTopology::Triangles;
		pipeDesc.cullMode = CullMode::Back;
		pipeDesc.depthTest = false;
		pipeDesc.depthWrite = false;
		pipeDesc.srcBlend = BlendFactor::One;
		pipeDesc.dstBlend = BlendFactor::Zero;

		m_DeferredPipelineDesc = pipeDesc;
		m_ScreenVertexLayout = ScreenVertexLayout;
	}


	void Setup(RenderGraphBuilder& builder) override
	{
		m_Albedo = builder.ReadTexture("Gbuffer.SceneColor");
		m_Velocity = builder.ReadTexture("Gbuffer.Velocity");
		m_Dep_Stencil = builder.ReadTexture("Gbuffer.Depth");
		m_Emissive = builder.ReadTexture("Gbuffer.Emissive");
		m_Normal = builder.ReadTexture("Gbuffer.Normal");
		m_MetallicRoughness = builder.ReadTexture("Gbuffer.MetallicRoughness");


		RDGTextureDesc colorDesc;
		colorDesc.width = m_Width;
		colorDesc.height = m_Height;
		colorDesc.format = Format::RGBA16F;
		colorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferSrc;

		m_DeferredLight = builder.CreateTexture("Light", colorDesc);


		builder.SetColorOutput(0, m_DeferredLight, RGLoadOp::Clear, { 0,0,0,1 });
		// builder.SetDepthOutput(m_Dep_Stencil, RGLoadOp::Clear, 1.0f);

		builder.Export(m_DeferredLight);
		// builder.Export(m_Dep_Stencil);

		if (currentcamera && currentcamera->skybox)
			currentcamera->skybox->InitializePipeline(builder, false);

		if (!m_DeferredPipeline)
		{
			m_DeferredPipelineDesc.descriptorSets = { m_DeferredDescriptorSet };
			m_DeferredPipeline = builder.CreatePipeline(
				m_DeferredLightShader, m_ScreenVertexLayout, &m_DeferredPipelineDesc
			);

			if (m_DeferredPipeline)
			{
				m_DeferredPipeline->SetupVertexFormat(ScreenVB);
				m_DeferredPipeline->SetupIndexBuffer(ScreenIB);
			}
		}
	}

	void Execute(RenderGraphContext& context) override
	{
		auto& cmd = context.GetCmd();


		cmd.SetDepthTest(false);
		cmd.SetDepthFunc(CompareOp::Always);

		const auto& frameCamera = m_FrameData->Camera;

		
		
		std::vector<GPULight> lights;
		glm::vec3 ambient(0.0f);
		bool hasAmbient = false;
		for (auto [entityID, trans, light] : m_Scene->m_Registry.view<Component::Transform, Component::Light>().each())
		{
			if (!hasAmbient && light.type == Component::Light::LightType::Directional)
			{
				ambient = light.Color * light.Intensity * light.Ambient;
				hasAmbient = true;
			}

			GPULight Glight;

			Glight.direc_ambient = { light.Direction,(float)light.Ambient };
			Glight.color_intens = { light.Color ,light.Intensity };
			Glight.pos_type = { trans.Position ,static_cast<float>(light.type) };
			Glight.Param = { light.Params.Data[0], light.Params.Data[1], light.Params.Data[2], light.Params.Data[3] };
			if (light.type == Component::Light::LightType::Area)
				Glight.Param.z = static_cast<float>(light.Params.Area.Shape);
			lights.push_back(Glight);
		}
		if (lights.size() > m_LightCapacity)
        {
            m_LightCapacity = static_cast<uint32_t>(lights.size());
            m_LightsData = RHIBuffer::Create(BufferDesc{
                static_cast<uint32_t>(m_LightCapacity * sizeof(GPULight)), BufferUsage::Storage, true, nullptr });
        }
        if (!lights.empty())
            m_LightsData->Upload(lights.data(), static_cast<uint32_t>(lights.size() * sizeof(GPULight)));

		DeferredLightDrawUBO dlduo{};
		dlduo.InvViewProjNoTrans = glm::inverse(frameCamera.JitteredViewProj);
		dlduo.View = frameCamera.View;
		dlduo.ViewPos = glm::vec4(currentcamera->getpos(), 0.0f);
		dlduo.ViewportSize = { m_Width,m_Height };

		dlduo.Near = currentcamera->getNear();
		dlduo.Far = currentcamera->getFar();
		dlduo.LightCount = static_cast<float>(lights.size());
		dlduo.Ambient = glm::vec4(ambient, 0.0f);



		m_DeferredLightingUBO->Upload(&dlduo, sizeof(dlduo));

		// Get GBuffer textures from RenderGraph
		RHITexture2D* normalTex = context.GetTexture(m_Normal);
		RHITexture2D* albedoTex = context.GetTexture(m_Albedo);
		RHITexture2D* specularTex = context.GetTexture(m_MetallicRoughness);
		RHITexture2D* depthTex = context.GetTexture(m_Dep_Stencil);

		m_DeferredDescriptorSet->Reset();
		m_DeferredDescriptorSet->BindUniformBuffer(0, m_DeferredLightingUBO, sizeof(DeferredLightDrawUBO));
		m_DeferredDescriptorSet->BindStorageBuffer(1, m_LightsData);
		m_DeferredDescriptorSet->BindTexture(10, normalTex ? normalTex : m_DefaultTex.get(), 10);
		m_DeferredDescriptorSet->BindTexture(11, albedoTex ? albedoTex : m_DefaultTex.get(), 11);
		m_DeferredDescriptorSet->BindTexture(12, specularTex ? specularTex : m_DefaultTex.get(), 12);
		m_DeferredDescriptorSet->BindTexture(13, depthTex ? depthTex : m_DefaultTex.get(), 13);
		m_DeferredDescriptorSet->BindTexture(14, context.GetTexture(m_Emissive), 14);
		m_DeferredDescriptorSet->BindTexture(15, context.GetTexture(m_Velocity), 15);
		m_DeferredDescriptorSet->Apply(0);

		// The lighting shader discards background pixels (depth == 1), so the skybox must be drawn first.
		currentcamera->RenderSkyBox(cmd);

		cmd.BindPipeline(m_DeferredPipeline);
		cmd.BindVertexBuffer(ScreenVB, 0);
		cmd.BindIndexBuffer(ScreenIB);
		const uint32_t dynamicOffset = 0;
		cmd.BindDescriptorSet(m_DeferredDescriptorSet, 0, &dynamicOffset, 1);

		cmd.DrawIndexed(6);
		m_DeferredPipeline->Unbind();
	}
	const char* GetName() const override { return "DeferredLight"; };

	void ValidateShaderLayout() const
	{
		if (!m_DeferredLightShader)
			return;

		const ShaderReflection& reflection = m_DeferredLightShader->GetReflection();
		if (const ShaderBlock* ubo = reflection.FindUniformBlock(0); ubo && ubo->Size != sizeof(DeferredLightDrawUBO))
		{
			Error_Core("[{} Pass]: DeferredLightDrawUBO is {} bytes but shader block '{}' is {} bytes",
				GetName(), sizeof(DeferredLightDrawUBO), ubo->Name, ubo->Size);
		}

		const ShaderBlock* ssbo = reflection.FindStorageBlock(1);
		if (ssbo && !ssbo->Members.empty() && ssbo->Members[0].ArrayStride != sizeof(GPULight))
		{
			Error_Core("[{} Pass]: GPULight is {} bytes but shader array '{}' has stride {}",
				GetName(), sizeof(GPULight), ssbo->Members[0].Name, ssbo->Members[0].ArrayStride);
		}
	}
	void SetViewportSize(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;
	}

	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;

	Ref<RHIBuffer> m_DeferredLightingUBO;
	Ref<RHIBuffer> m_LightsData;
	uint32_t m_LightCapacity = 128;


	Ref<RHIShader> m_DeferredLightShader;

	Ref<RHIDescriptorSet> m_DeferredDescriptorSet;
	Ref<RHIPipeline> m_DeferredPipeline;
	PipelineDesc m_DeferredPipelineDesc;
	VertexLayout m_ScreenVertexLayout;
	Ref<RHIBuffer> ScreenVB;
	Ref<RHIBuffer> ScreenIB;

	Ref<RHITexture2D>m_DefaultTex;

	RGTextureHandle m_Dep_Stencil;
	RGTextureHandle m_DeferredLight;
	RGTextureHandle m_Albedo;
	RGTextureHandle m_Velocity;
	RGTextureHandle m_Normal;
	RGTextureHandle m_MetallicRoughness;
	RGTextureHandle m_Emissive;



	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;

	struct DeferredLightDrawUBO
	{
		glm::mat4 InvViewProjNoTrans;
		glm::mat4 View;
		glm::vec4 ViewPos;
		glm::vec2 ViewportSize;
		glm::vec2 ShadowMapSize;
		float Near;
		float Far;
		float LightCount;
		float pad;
		glm::vec4 Ambient;
	};

	struct GPULight
	{
		glm::vec4 direc_ambient;
		glm::vec4 color_intens;
		glm::vec4 pos_type;
		glm::vec4 Param;
		
	};
};

REGISTER_RENDER_PASS(DeferredLightPass, "DeferredLight", "Base", 12, false )

// Keep the GBuffer inputs intact for lighting, then build the opaque scene
// attachments used by transparency, TAA and post processing.
class ForwardOpaquePass : public RenderGraphPass
{
public:
	ForwardOpaquePass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(std::move(scene)), m_FrameData(std::move(frameData)) {}

	const char* GetName() const override { return "ForwardOpaque"; }

	void SetViewportSize(uint32_t width, uint32_t height) override
	{
		m_Width = width;
		m_Height = height;
	}

	void Setup(RenderGraphBuilder& builder) override
	{
		m_Inputs = { builder.ReadTexture("DeferredLight.Light", RGAccess::CopySrc),
			builder.ReadTexture("Gbuffer.Velocity", RGAccess::CopySrc),
			builder.ReadTexture("Gbuffer.Depth", RGAccess::CopySrc) };
		m_Shadow = builder.ReadTexture("ShadowMap.Depth");

		RDGTextureDesc desc;
		desc.width = m_Width;
		desc.height = m_Height;
		desc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled |
			TextureUsage::TransferSrc | TextureUsage::TransferDst;
		desc.format = Format::RGBA16F;
		m_Outputs[0] = builder.CreateTexture("SceneColor", desc);
		desc.format = Format::RG16F;
		m_Outputs[1] = builder.CreateTexture("Velocity", desc);
		desc.format = Format::D24_UNORM_S8_UINT;
		desc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled |
			TextureUsage::TransferSrc | TextureUsage::TransferDst;
		desc.minFilter = FilterMode::Nearest;
		desc.magFilter = FilterMode::Nearest;
		m_Outputs[2] = builder.CreateTexture("Depth", desc);

		for (auto output : m_Outputs)
		{
			builder.WriteTexture(output, RGAccess::CopyDst);
			builder.Export(output);
		}
		builder.SetColorOutput(0, m_Outputs[0], RGLoadOp::Load);
		builder.SetColorOutput(1, m_Outputs[1], RGLoadOp::Load);
		builder.SetDepthOutput(m_Outputs[2], RGLoadOp::Load);

		PipelineDesc pipe;
		pipe.cullMode = CullMode::Back;
		pipe.depthTest = true;
		pipe.depthWrite = true;
		pipe.pushConstantSize = sizeof(DrawPushConstants);
		pipe.renderingSignature.colorFormats = { Format::RGBA16F, Format::RG16F };
		pipe.renderingSignature.depthFormat = Format::D24_UNORM_S8_UINT;
		m_Renderer.ConfigurePipeline(std::move(pipe));
	}

	void BeforeRender(RenderGraphContext& context) override
	{
		for (size_t i = 0; i < m_Inputs.size(); ++i)
			context.GetCmd().CopyTexture(context.GetTexture(m_Inputs[i]), context.GetTexture(m_Outputs[i]));
	}

	void Execute(RenderGraphContext& context) override
	{
		const ForwardMeshRenderer::Textures textures{
			m_Shadow.id != InvalidResourceId ? context.GetTexture(m_Shadow) : nullptr };
		m_Renderer.UploadFrame(*m_Scene, *m_FrameData, textures.shadow);
		auto& cmd = context.GetCmd();
		cmd.SetDepthTest(true);
		cmd.SetDepthFunc(CompareOp::Less);
		ForwardMeshRenderer::VisitReadyMeshes(*m_Scene, *m_FrameData,
			[&](const Ref<RHIMesh>& mesh, const Ref<Engine::Material>& material,
				const glm::mat4& model, const glm::mat4& previous)
			{
				const auto& reflection = material->GetShader()->GetReflection();
				if (!reflection.Transparent && reflection.RenderPass == MaterialRenderPass::Forward)
					m_Renderer.Draw(cmd, *mesh, material, model, previous, textures);
			});
	}

private:
	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	ForwardMeshRenderer m_Renderer;
	std::array<RGTextureHandle, 3> m_Inputs{}, m_Outputs{};
	RGTextureHandle m_Shadow;
	uint32_t m_Width = 0, m_Height = 0;
};
