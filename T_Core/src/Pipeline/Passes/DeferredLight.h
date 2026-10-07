#pragma once
#include<Pipeline/Passes/PassCommon.h>
#include "Pipeline/RenderPassRegistry.h"

class DeferredLightPass :public RenderGraphPass {
public:
	DeferredLightPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData) {

		constexpr uint32_t maxDrawObjects = 16;
		m_DeferredLightingUBO = RHIBuffer::Create(BufferDesc{ sizeof(DeferredLightDrawUBO) * maxDrawObjects, BufferUsage::Uniform, true, nullptr });
		constexpr uint32_t maxLightCapicity = 128;
		m_LightsData = RHIBuffer::Create(BufferDesc{ maxLightCapicity * sizeof(GPULight), BufferUsage::Storage, true, nullptr });
	
		m_DeferredLightShader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/DeferredLight.shader");
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
			//m_DeferredDescriptorSet->BindTexture(14, m_DefaultTex, 14);


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
		m_Normal = builder.ReadTexture("Gbuffer.Normal");
		m_Specular = builder.ReadTexture("Gbuffer.Specular");


		RDGTextureDesc colorDesc;
		colorDesc.width = m_Width;
		colorDesc.height = m_Height;
		colorDesc.format = Format::RGBA8_UNORM;
		colorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

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

		glm::mat4 view = currentcamera->GetViewFront();
		glm::mat4 proj = currentcamera->GetProj();
		glm::mat4 currentViewProj = proj * view;


		//if (EnableJitter)
		//	proj = Jittering(proj, (float)m_Width, (float)m_Height);

		
		
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
		m_LightsData->Upload(lights.data(), lights.size() * sizeof(GPULight));

		DeferredLightDrawUBO dlduo;	
		dlduo.InvViewProjNoTrans = glm::inverse(currentViewProj);
		dlduo.View = view;
		dlduo.ViewPos = glm::vec4(currentcamera->getpos(), 0.0f);
		dlduo.ViewportSize = { m_Width,m_Height };

		dlduo.Near = currentcamera->getNear();
		dlduo.Far = currentcamera->getFar();
		dlduo.LightCount = lights.size();
		dlduo.Ambient = glm::vec4(ambient, 0.0f);



		m_DeferredLightingUBO->Upload(&dlduo, sizeof(dlduo));

		// Get GBuffer textures from RenderGraph
		RHITexture2D* normalTex = context.GetTexture(m_Normal);
		RHITexture2D* albedoTex = context.GetTexture(m_Albedo);
		RHITexture2D* specularTex = context.GetTexture(m_Specular);
		RHITexture2D* depthTex = context.GetTexture(m_Dep_Stencil);

		m_DeferredDescriptorSet->Reset();
		m_DeferredDescriptorSet->BindUniformBuffer(0, m_DeferredLightingUBO, sizeof(DeferredLightDrawUBO));
		m_DeferredDescriptorSet->BindStorageBuffer(1, m_LightsData);
		m_DeferredDescriptorSet->BindTexture(10, normalTex ? normalTex : m_DefaultTex.get(), 10);
		m_DeferredDescriptorSet->BindTexture(11, albedoTex ? albedoTex : m_DefaultTex.get(), 11);
		m_DeferredDescriptorSet->BindTexture(12, specularTex ? specularTex : m_DefaultTex.get(), 12);
		m_DeferredDescriptorSet->BindTexture(13, depthTex ? depthTex : m_DefaultTex.get(), 13);
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
	RGTextureHandle m_Specular;



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

REGISTER_RENDER_PASS(DeferredLightPass, "DeferredLight", "Base", 12, true)