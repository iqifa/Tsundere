#pragma once
#include<Pipeline/Passes/PassCommon.h>
#include "Pipeline/RenderPassRegistry.h"

class GbufferPass :public RenderGraphPass
{
public:
	GbufferPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData) {



		constexpr uint32_t maxDrawObjects = 16;
		m_DrawUBO = RHIBuffer::Create(BufferDesc{ sizeof(GbufferDrawUBO) * maxDrawObjects, BufferUsage::Uniform, true, nullptr });
		m_FrameUBO = RHIBuffer::Create(BufferDesc{ sizeof(GbufferFrameUBO), BufferUsage::Uniform, true, nullptr });


		m_GbufferShader=RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/GBuffer.shader");


		InitFallbackGeometry();

		unsigned char white[4] = { 255, 255, 255, 255 };
		m_DefaultTex = RHITexture2D::Create({ 1, 1, Format::RGBA8_UNORM, FilterMode::Linear, FilterMode::Linear,
											 WrapMode::ClampToEdge, WrapMode::ClampToEdge, false, white });


		m_GbufferDescriptSet = RHIDescriptorSet::Create();
		if (m_GbufferDescriptSet)
		{
			m_GbufferDescriptSet->BindUniformBuffer(0, m_DrawUBO, sizeof(GbufferDrawUBO));
			m_GbufferDescriptSet->BindUniformBuffer(1, m_FrameUBO);
			m_GbufferDescriptSet->BindTexture(10, m_DefaultTex, 10);
			m_GbufferDescriptSet->BindTexture(11, m_DefaultTex, 11);
			m_GbufferDescriptSet->BindTexture(12, m_DefaultTex, 12);
			m_GbufferDescriptSet->BindTexture(13, m_DefaultTex, 13);

			m_GbufferDescriptSet->MarkBindingAsDynamic(0);
		}
		else {
			Error_Core("[GBuffer Pass]:m_GbufferDescriptSet is Null")
		}
	}

	const char* GetName() const override { return "Gbuffer"; }


	void Setup(RenderGraphBuilder& builder) override
	{
		m_ShadowMap = builder.ReadTexture("ShadowMap.Depth", RGAccess::ReadSRV);


		RDGTextureDesc colorDesc;
		colorDesc.width = m_Width;
		colorDesc.height = m_Height;
		colorDesc.format = Format::RGBA8_UNORM;
		colorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

		RDGTextureDesc velocityDesc = colorDesc;
		velocityDesc.format = Format::RG16F;

		RDGTextureDesc depthDesc;
		depthDesc.width = m_Width;
		depthDesc.height = m_Height;
		depthDesc.format = Format::D24_UNORM_S8_UINT;
		depthDesc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled;

		// Position and Normal need RGB16F for better precision and 3 components
		RDGTextureDesc posNormalDesc = colorDesc;
		posNormalDesc.format = Format::RGBA16F;  // Use float format for position/normal
		
		
		m_Position = builder.CreateTexture("Position", colorDesc);  // RGB16F

		m_SceneColor = builder.CreateTexture("SceneColor", colorDesc);
		m_Velocity = builder.CreateTexture("Velocity", velocityDesc);
		m_Depth = builder.CreateTexture("Depth", depthDesc);
		m_Normal = builder.CreateTexture("Normal", posNormalDesc);      // RGB16F (not RG16F!)
		m_Specular = builder.CreateTexture("Specular", colorDesc);


		builder.SetColorOutput(0, m_Position, RGLoadOp::Clear, { 0.0f,0.0f,0.0f,1.0f });
		builder.SetColorOutput(1, m_Normal, RGLoadOp::Clear, { 0.0f,0.0f,0.0f,1.0f });
		builder.SetColorOutput(2, m_SceneColor, RGLoadOp::Clear, { 1.0f,0.0f,0.0f,1.0f });
		builder.SetColorOutput(3, m_Specular, RGLoadOp::Clear, { 0.0f,0.0f,0.0f,1.0f });
		builder.SetColorOutput(4, m_Velocity, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 0.0f });
		builder.SetDepthOutput(m_Depth, RGLoadOp::Clear, 1.0f);


		builder.Export(m_SceneColor);
		builder.Export(m_Position);
		builder.Export(m_Normal);
		builder.Export(m_Specular);
		builder.Export(m_Velocity);
		builder.Export(m_Depth);
		

		//if (currentcamera && currentcamera->skybox)
		//	currentcamera->skybox->InitializePipeline(builder);

		if (!m_CubePipeline)
		{
			m_CubePipelineDesc.descriptorSets = { m_GbufferDescriptSet };
			m_CubePipeline = builder.CreatePipeline(
				m_GbufferShader, m_CubeVertexLayout, &m_CubePipelineDesc
			);

			if (m_CubePipeline)
			{
				m_CubePipeline->SetupVertexFormat(m_CubeVB);
				m_CubePipeline->SetupIndexBuffer(m_CubeIB);
			}
		}

		// Create pipeline for scene meshes (7-attribute vertex format with BoneIDs/Weights)
		if (!m_ScenePipeline)
		{
			VertexLayout sceneLayout;
			sceneLayout.stride = 18 * sizeof(float) + 4 * sizeof(int);  // 14 floats + 4 ints
			sceneLayout.attributes = {
				{ 0, VertexFormat::Float3, 0, 0 },                                     // Position
				{ 1, VertexFormat::Float3, 3 * sizeof(float), 0 },                    // Normal
				{ 2, VertexFormat::Float2, 6 * sizeof(float), 0 },                    // TexCoords
				{ 3, VertexFormat::Float3, 8 * sizeof(float), 0 },                    // Tangent
				{ 4, VertexFormat::Float3, 11 * sizeof(float), 0 },                   // Bitangent
				{ 5, VertexFormat::Int4,   14 * sizeof(float), 0 },                   // BoneIDs
				{ 6, VertexFormat::Float4, 18 * sizeof(float) + 4 * sizeof(int), 0 }, // Weights
			};

			PipelineDesc scenePipeDesc = m_CubePipelineDesc;  // Reuse base settings
			scenePipeDesc.descriptorSets = { m_GbufferDescriptSet };
			scenePipeDesc.vertexLayout = sceneLayout;
			m_ScenePipeline = builder.CreatePipeline(m_GbufferShader, sceneLayout, &scenePipeDesc);
		}
	}


	void Execute(RenderGraphContext& context) override
	{
		auto& cmd = context.GetCmd();

		// 1. ��ȡ������Դ
		RHITexture2D* shadowTex = m_ShadowMap.id != InvalidResourceId
			? context.GetTexture(m_ShadowMap)
			: nullptr;

		// 2. ������Ⱦ״̬
		cmd.SetDepthTest(true);
		cmd.SetDepthFunc(CompareOp::Less);

		// 3. ��ȡ�������
		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 currentViewProj = proj * view;

		if (EnableJitter)
			proj = Jittering(proj, (float)m_Width, (float)m_Height);

		//currentcamera->RenderSkyBox(cmd);

		GbufferFrameUBO fubo;
		fubo.hasNormalMap = 0;
		m_FrameUBO->Upload(&fubo, sizeof(fubo));

		m_GbufferShader->Bind();

		if (!m_GbufferDescriptSet)
			return;
		BindGeometryDescriptors(shadowTex);
		m_GbufferDescriptSet->Apply(0);

		bool drewSomething = false;
		m_DefaultTex->Bind(0);

		//// Bind scene pipeline once before drawing all scene objects
		if (m_ScenePipeline)
		{
			cmd.BindPipeline(m_ScenePipeline);
		}

		for (auto [entityID, transform, meshrender] : m_Scene->m_Registry.view<Component::Transform, Component::MeshRender>().each())
		{
			if (meshrender.ModelPath.empty() || meshrender.materials.empty())
				continue;

			Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
			if (!model || model->meshes.empty())
				continue;

			mat4 modelMat = transform.GetTransform();

			for (size_t i = 0; i < model->meshes.size(); i++)
			{
				auto& mesh = model->meshes[i];
				if (!mesh.IsGPUReady())
					continue;

				Ref<Material> mat = i < meshrender.materials.size()
					? meshrender.materials[i]
					: meshrender.materials[0];

					mat->Render(m_GbufferShader);

					// Per-draw UBO
					GbufferDrawUBO dubo;
					dubo.MVP_matrix = proj * view * modelMat;
					dubo.model = modelMat;
					dubo.prevModel = modelMat;
					dubo.viewProj = currentViewProj;
					dubo.prevViewProj = m_PrevViewProjMatrix;
					m_DrawUBO->Upload(&dubo, sizeof(dubo));

					BindGeometryDescriptors(shadowTex);
					m_GbufferDescriptSet->Apply(0);
					cmd.BindDescriptorSet(m_GbufferDescriptSet, 0);

					mesh.gpuMesh->Draw(cmd);
					drewSomething = true;
			}
		}
	
		if (!drewSomething)
		{
			DrawFallbackCube(cmd, proj, view, currentViewProj, shadowTex);
		}

		m_PrevViewProjMatrix = currentViewProj;
		m_FrameCount++;
	}
	bool EnableJitter = false;
	void SetViewportSize(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;
	}

private:
	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	Ref<RHIBuffer> m_DrawUBO;
	Ref<RHIBuffer> m_FrameUBO;
	Ref<RHITexture2D> m_DefaultTex;
	Ref<RHIDescriptorSet> m_GbufferDescriptSet;

	RGTextureHandle m_ShadowMap;
	RGTextureHandle m_SceneColor;
	RGTextureHandle m_Velocity;
	RGTextureHandle m_Depth;
	RGTextureHandle m_Position;
	RGTextureHandle m_Normal;
	RGTextureHandle m_Specular;



	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;
	int m_FrameCount = 0;
	mat4 m_PrevViewProjMatrix = mat4(1.0f);

	Ref<RHIBuffer> m_CubeVB;
	Ref<RHIBuffer> m_CubeIB;
	Ref<RHIPipeline> m_CubePipeline;
	Ref<RHIPipeline> m_ScenePipeline;  // Pipeline for scene meshes (7-attribute format)
	VertexLayout m_CubeVertexLayout;
	PipelineDesc m_CubePipelineDesc;
	Ref<RHIShader> m_GbufferShader;




	struct GbufferDrawUBO
	{
		glm::mat4 MVP_matrix;
		glm::mat4 model;
		glm::mat4 prevModel;
		glm::mat4 viewProj;
		glm::mat4 prevViewProj;
	};

	struct GbufferFrameUBO
	{
		int32_t hasNormalMap;
		int32_t _pad[3];
	};

	void InitFallbackGeometry() {
		float position[] =
		{
			// Front face (z = -0.5), normal (0, 0, -1)
			-0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,
			 0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,
			 0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,
			-0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,

			// Back face (z = 0.5), normal (0, 0, 1)
			-0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 0.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,
			 0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 0.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,
			 0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,
			-0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f,

			// Left face (x = -0.5), normal (-1, 0, 0)
			-0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f,
			-0.5f, -0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f,
			-0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 1.0f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f,
			-0.5f,  0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f,

			// Right face (x = 0.5), normal (1, 0, 0)
			 0.5f, -0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f, 1.0f, 0.0f,
			 0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f, 1.0f, 0.0f,
			 0.5f,  0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 1.0f,   0.0f, 0.0f,-1.0f,   0.0f, 1.0f, 0.0f,
			 0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 1.0f,   0.0f, 0.0f,-1.0f,   0.0f, 1.0f, 0.0f,

			 // Top face (y = 0.5), normal (0, 1, 0)
			 -0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,
			  0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,
			  0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,
			 -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,

			 // Bottom face (y = -0.5), normal (0, -1, 0)
			 -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,
			  0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,
			  0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,
			 -0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f
		};

		unsigned int indices[] = {
			0,  2,  1,      3,  2,  0,
			4,  5,  6,      6,  7,  4,
			8,  9, 10,     10, 11,  8,
			12, 13, 14,    14, 15, 12,
			16, 17, 18,    18, 19, 16,
			20, 21, 22,    22, 23, 20
		};

		m_CubeVB = RHIBuffer::Create({ 24 * 14 * (uint32_t)sizeof(float), BufferUsage::Vertex, false, position });
		m_CubeIB = RHIBuffer::Create({ 36 * (uint32_t)sizeof(unsigned int), BufferUsage::Index, false, indices });


		VertexLayout vtxLayout;
		vtxLayout.stride = 14 * sizeof(float);
		vtxLayout.attributes = {
			{ 0, VertexFormat::Float3, 0, 0 },
			{ 1, VertexFormat::Float3, 3 * sizeof(float), 0 },
			{ 2, VertexFormat::Float2, 6 * sizeof(float), 0 },
			{ 3, VertexFormat::Float3, 8 * sizeof(float), 0 },
			{ 4, VertexFormat::Float3, 11 * sizeof(float), 0 },
		};


		PipelineDesc pipeDesc;
		pipeDesc.shader = m_GbufferShader;
		pipeDesc.vertexLayout = vtxLayout;
		pipeDesc.topology = PrimitiveTopology::Triangles;
		pipeDesc.cullMode = CullMode::Back;
		pipeDesc.depthTest = true;
		pipeDesc.depthWrite = true;
		pipeDesc.srcBlend = BlendFactor::One;
		pipeDesc.dstBlend = BlendFactor::Zero;

		m_CubeVertexLayout = vtxLayout;
		m_CubePipelineDesc = pipeDesc;
	}

	mat4 Jittering(const mat4& originalProj, float width, float height)
	{
		int jitterIndex = m_FrameCount % 16;
		vec2 currentJitter = GetHaltonJitter(jitterIndex);

		float deltaX = currentJitter.x * 2.0f / width;
		float deltaY = currentJitter.y * 2.0f / height;

		mat4 jitteredProjMatrix = originalProj;
		jitteredProjMatrix[2][0] += deltaX;
		jitteredProjMatrix[2][1] += deltaY;

		return jitteredProjMatrix;
	}
	vec2 GetHaltonJitter(int index)
	{
		auto halton = [](int index, int base) -> float {
			float f = 1.0f;
			float r = 0.0f;
			int current = index;
			while (current > 0) {
				f = f / base;
				r = r + f * (current % base);
				current = current / base;
			}
			return r;
			};

		return vec2(halton(index + 1, 2) - 0.5f, halton(index + 1, 3) - 0.5f);
	}

	void BindGeometryDescriptors(RHITexture2D* shadowTex)
	{
		m_GbufferDescriptSet->Reset();
		m_GbufferDescriptSet->BindUniformBuffer(0, m_DrawUBO, sizeof(GbufferDrawUBO));
		m_GbufferDescriptSet->BindUniformBuffer(1, m_FrameUBO);
		m_GbufferDescriptSet->BindTexture(10, m_DefaultTex, 10);
		m_GbufferDescriptSet->BindTexture(11, m_DefaultTex, 11);
		m_GbufferDescriptSet->BindTexture(12, m_DefaultTex, 12);

		if (shadowTex)
			m_GbufferDescriptSet->BindTexture(13, shadowTex, 13);
		else
			m_GbufferDescriptSet->BindTexture(13, m_DefaultTex, 13);
	}
	void DrawFallbackCube(
		RHICommandBuffer& cmd,
		const mat4& proj,
		const mat4& view,
		const mat4& currentViewProj,
		RHITexture2D* shadowTex)
	{
		m_GbufferShader->Bind();
		m_DefaultTex->Bind(10);
		m_DefaultTex->Bind(11);
		m_DefaultTex->Bind(12);

		mat4 model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
		mat4 mvp = proj * view * model;

		GbufferDrawUBO dubo;
		dubo.MVP_matrix = mvp;
		dubo.model = model;
		dubo.prevModel = model;
		dubo.viewProj = currentViewProj;
		dubo.prevViewProj = m_PrevViewProjMatrix;
		m_DrawUBO->Upload(&dubo, sizeof(dubo), 0);  // Upload to offset 0

		BindGeometryDescriptors(shadowTex);
		m_GbufferDescriptSet->Apply(0);

		cmd.BindPipeline(m_CubePipeline);
		cmd.BindVertexBuffer(m_CubeVB, 0);
		cmd.BindIndexBuffer(m_CubeIB);

		// Dynamic UBO requires offset even when reading from position 0
		uint32_t dynamicOffset = 0;
		cmd.BindDescriptorSet(m_GbufferDescriptSet, 0, &dynamicOffset, 1);
		cmd.DrawIndexed(36);
		// Floor
		mat4 floorModel = translate(mat4(1.0f), vec3(0.0f, -2.0f, 0.0f));
		floorModel = scale(floorModel, vec3(10.0f, 0.05f, 10.0f));
		mat4 floorMVP = proj * view * floorModel;
		dubo.MVP_matrix = floorMVP;
		dubo.model = floorModel;
		dubo.prevModel = floorModel;
		m_DrawUBO->Upload(&dubo, sizeof(dubo), sizeof(dubo));

		BindGeometryDescriptors(shadowTex);
		m_GbufferDescriptSet->Apply(0);

		// Use dynamic offset to read from offset position in UBO
		dynamicOffset = sizeof(dubo);
		cmd.BindDescriptorSet(m_GbufferDescriptSet, 0, &dynamicOffset, 1);
		cmd.DrawIndexed(36);
		m_CubePipeline->Unbind();
	}

};

REGISTER_RENDER_PASS(GbufferPass, "Gbuffer", "Base", 11, true)