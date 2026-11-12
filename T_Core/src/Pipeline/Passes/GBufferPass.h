#pragma once
#include<Pipeline/Passes/PassCommon.h>
#include "Pipeline/RenderPassRegistry.h"
#include "Pipeline/Passes/ForwardMeshRenderer.h"
#include "Scene/Vertex.h"
#include "Core/MaterialLibrary.h"
#include <cstddef>
#include <cstring>
#include <vector>
#include <unordered_map>

class GbufferPass :public RenderGraphPass
{
public:
	GbufferPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData) {



		m_CameraUBO = RHIBuffer::Create(BufferDesc{ sizeof(CameraUBO), BufferUsage::Uniform, true, nullptr });
		m_FrameUBO = RHIBuffer::Create(BufferDesc{ sizeof(GbufferFrameUBO), BufferUsage::Uniform, true, nullptr });


		m_GbufferShader=RHIShader::Create("res/shaders/GBuffer.shader");

		InitDefaultMaterialUBO();

		InitFallbackGeometry();

		unsigned char white[4] = { 255, 255, 255, 255 };
		m_DefaultTex = RHITexture2D::Create({ 1, 1, Format::RGBA8_UNORM, FilterMode::Linear, FilterMode::Linear,
											 WrapMode::ClampToEdge, WrapMode::ClampToEdge, false, white });


		m_GbufferDescriptSet = RHIDescriptorSet::Create();
		if (m_GbufferDescriptSet)
		{
			m_GbufferDescriptSet->BindUniformBuffer(1, m_FrameUBO);
			m_GbufferDescriptSet->BindUniformBuffer(2, m_CameraUBO);
			// Establish the material-block layout binding before the pipeline
			// freezes the descriptor layout in Setup(). Per-draw bindings
			// replace it with the material's own UBO.
			if (m_DefaultMaterialUBO)
				m_GbufferDescriptSet->BindUniformBuffer(3, m_DefaultMaterialUBO);
			m_GbufferDescriptSet->BindTexture(10, m_DefaultTex, 10);
			m_GbufferDescriptSet->BindTexture(11, m_DefaultTex, 11);
			m_GbufferDescriptSet->BindTexture(12, m_DefaultTex, 12);
			m_GbufferDescriptSet->BindTexture(13, m_DefaultTex, 13);
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
		colorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferSrc;

		RDGTextureDesc velocityDesc = colorDesc;
		velocityDesc.format = Format::RG16F;

		RDGTextureDesc depthDesc;
		depthDesc.width = m_Width;
		depthDesc.height = m_Height;
		depthDesc.format = Format::D24_UNORM_S8_UINT;
		depthDesc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled | TextureUsage::TransferSrc;
		depthDesc.minFilter = FilterMode::Nearest;
		depthDesc.magFilter = FilterMode::Nearest;

		RDGTextureDesc normalDesc = colorDesc;
		normalDesc.format = Format::RGBA16F;

		m_SceneColor = builder.CreateTexture("SceneColor", colorDesc);
		m_Velocity = builder.CreateTexture("Velocity", velocityDesc);
		m_Depth = builder.CreateTexture("Depth", depthDesc);
		m_Normal = builder.CreateTexture("Normal", normalDesc);
		m_MetallicRoughness = builder.CreateTexture("MetallicRoughness", velocityDesc);

		builder.SetColorOutput(0, m_Normal, RGLoadOp::Clear, { 0.0f,0.0f,0.0f,1.0f });
		builder.SetColorOutput(1, m_SceneColor, RGLoadOp::Clear, { 1.0f,0.0f,0.0f,1.0f });
		builder.SetColorOutput(2, m_MetallicRoughness, RGLoadOp::Clear, { 0.0f,0.0f,0.0f,1.0f });
		builder.SetColorOutput(3, m_Velocity, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 0.0f });
		m_Emissive = builder.CreateTexture("Emissive", normalDesc);
		builder.SetColorOutput(4, m_Emissive, RGLoadOp::Clear, { 0,0,0,0 });
		builder.Export(m_Emissive);
		builder.SetDepthOutput(m_Depth, RGLoadOp::Clear, 1.0f);

		builder.Export(m_SceneColor);
		builder.Export(m_Normal);
		builder.Export(m_MetallicRoughness);
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
		PipelineDesc materialDesc = m_CubePipelineDesc;
		materialDesc.renderingSignature.colorFormats = {
			Format::RGBA16F, Format::RGBA8_UNORM, Format::RG16F, Format::RG16F, Format::RGBA16F };
		materialDesc.renderingSignature.depthFormat = Format::D24_UNORM_S8_UINT;
		m_MaterialRenderer.ConfigurePipeline(std::move(materialDesc));

		if (!m_ScenePipeline)
		{
			VertexLayout sceneLayout;
			sceneLayout.stride = sizeof(Vertex);
			sceneLayout.attributes = {
				{ 0, VertexFormat::Float3, offsetof(Vertex, Position) },
				{ 1, VertexFormat::Float3, offsetof(Vertex, Normal) },
				{ 2, VertexFormat::Float2, offsetof(Vertex, TexCoords) },
				{ 3, VertexFormat::Float3, offsetof(Vertex, Tangent) },
				{ 4, VertexFormat::Float3, offsetof(Vertex, Bitangent) },
				{ 5, VertexFormat::Int4,   offsetof(Vertex, m_BoneIDs) },
				{ 6, VertexFormat::Float4, offsetof(Vertex, m_Weights) },
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
		const auto& frameCamera = m_FrameData->Camera;

		//currentcamera->RenderSkyBox(cmd);

		GbufferFrameUBO fubo{};
		fubo.hasNormalMap = 0;
		m_FrameUBO->Upload(&fubo, sizeof(fubo));

		CameraUBO camera;
		camera.jitteredViewProj = frameCamera.JitteredViewProj;
		camera.viewProj = frameCamera.ViewProj;
		camera.prevViewProj = frameCamera.PrevViewProj;
		m_CameraUBO->Upload(&camera, sizeof(camera));

		m_GbufferShader->Bind();

		if (!m_GbufferDescriptSet)
			return;
		BindGeometryDescriptors(shadowTex);
		m_GbufferDescriptSet->Apply(0);

		bool hadReadyMesh = false;
		m_DefaultTex->Bind(0);

		m_MaterialRenderer.UploadFrame(*m_Scene, *m_FrameData, shadowTex);
		for (auto [entityID, transform, meshrender] : m_Scene->m_Registry.view<Component::Transform, Component::MeshRender>().each())
		{
			if (meshrender.ModelPath.empty() || meshrender.materials.empty())
				continue;

			Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
			if (!model || model->meshes.empty())
				continue;

			glm::mat4 modelMat = transform.GetTransform();
			const glm::mat4 prevModel = m_FrameData->PreviousModel(entityID, modelMat);

			for (size_t i = 0; i < model->meshes.size(); i++)
			{
				auto& mesh = model->meshes[i];
				if (!mesh.IsGPUReady())
					continue;
				hadReadyMesh = true;

				Ref<Engine::Material> mat = i < meshrender.materials.size()
					? meshrender.materials[i]
					: meshrender.materials[0];
				if (mat && mat->GetShader())
				{
					const auto& reflection = mat->GetShader()->GetReflection();
					if (reflection.Transparent || reflection.RenderPass == MaterialRenderPass::Forward)
						continue;
					if (reflection.RenderPass == MaterialRenderPass::GBuffer)
					{
						m_MaterialRenderer.Draw(cmd, *mesh.gpuMesh, mat, modelMat, prevModel, { shadowTex });
						continue;
					}
				}
				if (!m_ScenePipeline)
					continue;
				cmd.BindPipeline(m_ScenePipeline);

				// Material textures override the white defaults; the shader's
				// reflected sampler names (texture_diffuse1/...) pick the slots.
				RHITexture2D* diffuse = nullptr;
				RHITexture2D* specular = nullptr;
				RHITexture2D* normal = nullptr;
				ResolveMaterialTextures(mat, diffuse, specular, normal);

				if (mat) mat->ApplyToShader(m_GbufferShader);

				BindGeometryDescriptors(shadowTex);
				if (diffuse)  m_GbufferDescriptSet->BindTexture(10, diffuse, 10);
				if (specular) m_GbufferDescriptSet->BindTexture(11, specular, 11);
				if (normal)   m_GbufferDescriptSet->BindTexture(12, normal, 12);
				if (Ref<RHIBuffer> matUBO = AcquireMaterialUBO(mat))
					m_GbufferDescriptSet->BindUniformBuffer(3, matUBO);

				// Normal-map presence must match what is actually bound.
				GbufferFrameUBO fubo{};
				fubo.hasNormalMap = normal ? 1 : 0;
				m_FrameUBO->Upload(&fubo, sizeof(fubo));

				m_GbufferDescriptSet->Apply(0);
				cmd.BindDescriptorSet(m_GbufferDescriptSet, 0);
				PushDrawModel(cmd, modelMat, prevModel);

				mesh.gpuMesh->Draw(cmd);
			}
		}
	
		if (!hadReadyMesh)
		{
			DrawFallbackCube(cmd, shadowTex);
		}

	}
	void SetViewportSize(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;
	}

private:
	struct MaterialUBOEntry
	{
		Ref<RHIBuffer> buffer;
		std::vector<char> bytes;
		uint32_t size = 0;
	};
	std::unordered_map<const Engine::Material*, MaterialUBOEntry> m_MaterialUBOs;

	// Identity material parameters for draws without a material block: white
	// base color, unit tiling, roughness 0.5, metallic 0, emissive 0.
	Ref<RHIBuffer> m_DefaultMaterialUBO;
	std::vector<char> m_DefaultMaterialBytes;

	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	Ref<RHIBuffer> m_CameraUBO;
	Ref<RHIBuffer> m_FrameUBO;
	Ref<RHITexture2D> m_DefaultTex;
	Ref<RHIDescriptorSet> m_GbufferDescriptSet;

	RGTextureHandle m_ShadowMap;
	RGTextureHandle m_SceneColor;
	RGTextureHandle m_Velocity;
	RGTextureHandle m_Depth;
	RGTextureHandle m_Normal;
	RGTextureHandle m_MetallicRoughness;
	RGTextureHandle m_Emissive;



	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;

	Ref<RHIBuffer> m_CubeVB;
	Ref<RHIBuffer> m_CubeIB;
	Ref<RHIPipeline> m_CubePipeline;
	ForwardMeshRenderer m_MaterialRenderer{ true };
	Ref<RHIPipeline> m_ScenePipeline;  // Pipeline for scene meshes (7-attribute format)
	VertexLayout m_CubeVertexLayout;
	PipelineDesc m_CubePipelineDesc;
	Ref<RHIShader> m_GbufferShader;




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
		pipeDesc.pushConstantSize = sizeof(DrawPushConstants);

		m_CubeVertexLayout = vtxLayout;
		m_CubePipelineDesc = pipeDesc;
	}


	// Material textures for one draw. First tries the GBuffer shader's own
	// sampler names (texture_diffuse1/...), then falls back to scanning the
	// material's texture parameters by role keyword — a material created from
	// a different shader stores its texture under e.g. "u_BaseColorMap".
	void ResolveMaterialTextures(
		const Ref<Engine::Material>& mat,
		RHITexture2D*& diffuse,
		RHITexture2D*& specular,
		RHITexture2D*& normal)
	{
		diffuse = specular = normal = nullptr;

		if (!mat)
			return;

		// Collect the material's texture parameters (name, texture) once.
		std::vector<std::pair<const std::string*, RHITexture2D*>> matTextures;
		for (const auto& [name, param] : mat->GetParameters())
		{
			if (param.type != Engine::MaterialParameter::Type::Texture2D)
				continue;
			const auto tex = param.Get<Ref<RHITexture2D>>();
			if (tex && *tex)
				matTextures.push_back({ &name, (*tex).get() });
		}
		if (matTextures.empty())
			return;

		auto findByName = [&](const std::string& samplerName) -> RHITexture2D*
		{
			for (const auto& [name, tex] : matTextures)
				if (*name == samplerName)
					return tex;
			return nullptr;
		};
		auto findByRole = [&](const char* keyword) -> RHITexture2D*
		{
			for (const auto& [name, tex] : matTextures)
			{
				std::string lower = *name;
				std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
				if (lower.find(keyword) != std::string::npos)
					return tex;
			}
			return nullptr;
		};

		const ShaderReflection& refl = m_GbufferShader->GetReflection();
		for (const auto& sampler : refl.SampledImages)
		{
			RHITexture2D* tex = findByName(sampler.Name);
			if (!tex)
			{
				if (sampler.Name.find("diffuse") != std::string::npos)
				{
					tex = findByRole("diffuse");
					if (!tex) tex = findByRole("basecolor");
					if (!tex) tex = findByRole("albedo");
				}
				if (!tex && sampler.Name.find("specular") != std::string::npos)
				{
					tex = findByRole("specular");


				}
				if (!tex && sampler.Name.find("normal") != std::string::npos)
				{
					tex = findByRole("normal");
					if (!tex) tex = findByRole("bump");
				}
			}

			if (!tex)
				continue;
			if (sampler.Name.find("diffuse") != std::string::npos)
				diffuse = tex;
			else if (sampler.Name.find("specular") != std::string::npos)
				specular = tex;
			else if (sampler.Name.find("normal") != std::string::npos)
				normal = tex;
		}
	}

	// Packed material-parameter UBO for one draw (MaterialParams, binding 3).
	// One buffer per material, rebuilt when the packed bytes change. Materials
	// without a material block fall back to the identity defaults.
	Ref<RHIBuffer> AcquireMaterialUBO(const Ref<Engine::Material>& mat)
	{
		if (!m_GbufferShader)
			return m_DefaultMaterialUBO;

		if (mat)
		{
			std::vector<char> packed;
			if (mat->PackMaterialBlock(m_GbufferShader->GetReflection(), packed) > 0)
			{
                // Legacy materials may omit any built-in parameter. Preserve defaults
                // for missing values instead of zeroing baseColor/tiling/roughness.
                for (const auto& block : m_GbufferShader->GetReflection().UniformBlocks)
                    if (block.Binding == 3)
                        for (const auto& member : block.Members)
                            if (!mat->GetParameter(member.Name) && member.Offset + member.Size <= m_DefaultMaterialBytes.size())
                                std::memcpy(packed.data() + member.Offset,
                                    m_DefaultMaterialBytes.data() + member.Offset, member.Size);
				auto& entry = m_MaterialUBOs[mat.get()];
				if (!entry.buffer || entry.size != packed.size() ||
					std::memcmp(entry.bytes.data(), packed.data(), packed.size()) != 0)
				{
					if (!entry.buffer || entry.size != packed.size())
					{
						entry.buffer = RHIBuffer::Create(BufferDesc{
							(uint32_t)packed.size(), BufferUsage::Uniform, true, packed.data() });
					}
					else
					{
						entry.buffer->Upload(packed.data(), (uint32_t)packed.size());
					}
					entry.bytes = std::move(packed);
					entry.size = (uint32_t)entry.bytes.size();
				}
				return entry.buffer;
			}
		}
		return m_DefaultMaterialUBO;
	}

	// Builds the identity material UBO once, sized from the shader's
	// MaterialParams block. Call after m_GbufferShader exists.
	void InitDefaultMaterialUBO()
	{
		if (!m_GbufferShader || m_DefaultMaterialUBO)
			return;

		std::vector<char> packed;
		if (m_GbufferShader->GetReflection().UniformBlocks.empty())
			return;

		// Identity values in the block's declared layout: pack through an
		// anonymous material holding the editor defaults.
		auto shader = m_GbufferShader;
		Ref<Engine::Material> identity = Engine::MaterialLibrary::CreateDefault("Identity", shader);
		if (identity->PackMaterialBlock(shader->GetReflection(), packed) == 0)
			return;

		m_DefaultMaterialUBO = RHIBuffer::Create(BufferDesc{
			(uint32_t)packed.size(), BufferUsage::Uniform, true, packed.data() });
		m_DefaultMaterialBytes = std::move(packed);
	}

	void BindGeometryDescriptors(RHITexture2D* shadowTex)
	{
		m_GbufferDescriptSet->Reset();
		m_GbufferDescriptSet->BindUniformBuffer(1, m_FrameUBO);
		m_GbufferDescriptSet->BindUniformBuffer(2, m_CameraUBO);
		if (m_DefaultMaterialUBO)
			m_GbufferDescriptSet->BindUniformBuffer(3, m_DefaultMaterialUBO);
		m_GbufferDescriptSet->BindTexture(10, m_DefaultTex, 10);
		m_GbufferDescriptSet->BindTexture(11, m_DefaultTex, 11);
		m_GbufferDescriptSet->BindTexture(12, m_DefaultTex, 12);

		if (shadowTex)
			m_GbufferDescriptSet->BindTexture(13, shadowTex, 13);
		else
			m_GbufferDescriptSet->BindTexture(13, m_DefaultTex, 13);
	}

	void DrawFallbackCube(RHICommandBuffer& cmd, RHITexture2D* shadowTex)
	{
		m_GbufferShader->Bind();
		m_DefaultTex->Bind(10);
		m_DefaultTex->Bind(11);
		m_DefaultTex->Bind(12);

		BindGeometryDescriptors(shadowTex);
		m_GbufferDescriptSet->Apply(0);

		cmd.BindPipeline(m_CubePipeline);
		cmd.BindVertexBuffer(m_CubeVB, 0);
		cmd.BindIndexBuffer(m_CubeIB);
		cmd.BindDescriptorSet(m_GbufferDescriptSet, 0);

		glm::mat4 model = scale(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 1.0f));
		PushDrawModel(cmd, model, model);
		cmd.DrawIndexed(36);

		glm::mat4 floorModel = translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, 0.0f));
		floorModel = scale(floorModel, glm::vec3(10.0f, 0.05f, 10.0f));
		PushDrawModel(cmd, floorModel, floorModel);
		cmd.DrawIndexed(36);
		m_CubePipeline->Unbind();
	}

};

REGISTER_RENDER_PASS(GbufferPass, "Gbuffer", "Base", 11, false)
