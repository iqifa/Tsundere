#pragma once
#include "Pipeline/Passes/ForwardMeshRenderer.h"
#include "Pipeline/RenderGraphPass.h"
#include "Pipeline/RenderPassRegistry.h"

class GeometryPass : public RenderGraphPass
{
public:
	GeometryPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData)
	{
		m_LitShader = RHIShader::Create("res/shaders/Lit.shader");
		InitFallbackGeometry();
		m_GeometryDescriptorSet = RHIDescriptorSet::Create();
	}

	const char* GetName() const override { return "Geometry"; }

	void Setup(RenderGraphBuilder& builder) override
	{
		// 1. 读取可选的 ShadowMap（如果不存在，返回 invalid handle）
		m_ShadowMap = builder.ReadTexture("ShadowMap.Depth", RGAccess::ReadSRV);

		// 2. 创建输出资源（自动命名为 "Geometry.SceneColor" 等）
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

		m_SceneColor = builder.CreateTexture("SceneColor", colorDesc);
		m_Velocity = builder.CreateTexture("Velocity", velocityDesc);
		m_Depth = builder.CreateTexture("Depth", depthDesc);

		// 3. 声明渲染目标（内部会注册 Write 依赖）
		// MSAA 时渲染到多重采样附件，pass 结束时 resolve 到上面的单采样纹理，下游 pass 不受影响。
		if (m_SampleCount > 1)
		{
			RDGTextureDesc colorMSDesc = colorDesc;
			colorMSDesc.sampleCount = m_SampleCount;
			colorMSDesc.usage = TextureUsage::ColorAttachment;

			RDGTextureDesc velocityMSDesc = colorMSDesc;
			velocityMSDesc.format = Format::RG16F;

			RDGTextureDesc depthMSDesc = depthDesc;
			depthMSDesc.sampleCount = m_SampleCount;
			depthMSDesc.usage = TextureUsage::DepthStencil;

			RGTextureHandle sceneColorMS = builder.CreateTexture("SceneColorMS", colorMSDesc);
			RGTextureHandle velocityMS = builder.CreateTexture("VelocityMS", velocityMSDesc);
			RGTextureHandle depthMS = builder.CreateTexture("DepthMS", depthMSDesc);

			builder.SetColorOutput(0, sceneColorMS, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 1.0f });
			builder.SetColorOutput(1, velocityMS, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 0.0f });
			builder.SetDepthOutput(depthMS, RGLoadOp::Clear, 1.0f);

			builder.SetColorResolve(0, m_SceneColor);
			builder.SetColorResolve(1, m_Velocity);
			builder.SetDepthResolve(m_Depth);
		}
		else
		{
			builder.SetColorOutput(0, m_SceneColor, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 1.0f });
			builder.SetColorOutput(1, m_Velocity, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 0.0f });
			builder.SetDepthOutput(m_Depth, RGLoadOp::Clear, 1.0f);
		}

		// 这些资源会在图执行结束后被 ExampleLayer 读取并显示。
		builder.Export(m_SceneColor);
		builder.Export(m_Velocity);

		// Derive the pipeline rendering signature after declaring all outputs.
		if (currentcamera && currentcamera->skybox)
			currentcamera->skybox->InitializePipeline(builder);
		if (!m_CubePipeline)
		{
			BindGeometryDescriptors(nullptr);
			m_CubePipelineDesc.descriptorSets = { m_GeometryDescriptorSet };
			m_CubePipeline = builder.CreatePipeline(
				m_LitShader, m_CubeVertexLayout, &m_CubePipelineDesc);
			if (m_CubePipeline)
			{
				m_CubePipeline->SetupVertexFormat(m_CubeVB);
				m_CubePipeline->SetupIndexBuffer(m_CubeIB);
			}
		}

		PipelineDesc desc = m_CubePipelineDesc;
		desc.renderingSignature.colorFormats = { Format::RGBA8_UNORM, Format::RG16F };
		desc.renderingSignature.depthFormat = Format::D24_UNORM_S8_UINT;
		desc.renderingSignature.sampleCount = m_SampleCount;
		m_MeshRenderer.ConfigurePipeline(std::move(desc));
	}

	void Execute(RenderGraphContext& context) override
	{
		auto& cmd = context.GetCmd();
		RHITexture2D* shadow = m_ShadowMap.id != InvalidResourceId
			? context.GetTexture(m_ShadowMap) : nullptr;
		cmd.SetDepthTest(true);
		cmd.SetDepthFunc(CompareOp::Less);
		currentcamera->RenderSkyBox(cmd);
		m_MeshRenderer.UploadFrame(*m_Scene, *m_FrameData, shadow);
		const ForwardMeshRenderer::Textures textures{ shadow };
		const bool hadReadyMesh = ForwardMeshRenderer::VisitReadyMeshes(*m_Scene, *m_FrameData,
			[&](const Ref<RHIMesh>& mesh, const Ref<Engine::Material>& material,
				const glm::mat4& model, const glm::mat4& previous)
			{
				if (!material->GetShader()->GetReflection().Transparent)
					m_MeshRenderer.Draw(cmd, *mesh, material, model, previous, textures);
			});
		if (!hadReadyMesh && m_CubePipeline)
			DrawFallbackCube(cmd, shadow);
	}

	void SetViewportSize(uint32_t width, uint32_t height) override
	{
		m_Width = width;
		m_Height = height;
	}

	void SetSampleCount(uint32_t samples)
	{
		if (samples == m_SampleCount)
			return;
		m_SampleCount = samples;
		m_CubePipeline.reset();
	}

	uint32_t GetSampleCount() const { return m_SampleCount; }

private:
	void BindGeometryDescriptors(RHITexture2D* shadowTex)
	{
		m_GeometryDescriptorSet->Reset();
		m_MeshRenderer.BindFrameDescriptors(*m_GeometryDescriptorSet);
		m_GeometryDescriptorSet->BindTexture(10, m_MeshRenderer.DefaultTexture(), 10);
		m_GeometryDescriptorSet->BindTexture(11, m_MeshRenderer.DefaultTexture(), 11);
		m_GeometryDescriptorSet->BindTexture(12, m_MeshRenderer.DefaultTexture(), 12);

		if (shadowTex)
			m_GeometryDescriptorSet->BindTexture(13, shadowTex, 13);
		else
			m_GeometryDescriptorSet->BindTexture(13, m_MeshRenderer.DefaultTexture(), 13);
	}

	void InitFallbackGeometry()
	{
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
		pipeDesc.shader = m_LitShader;
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

	void DrawFallbackCube(RHICommandBuffer& cmd, RHITexture2D* shadowTex)
	{
		m_LitShader->Bind();
		m_MeshRenderer.DefaultTexture()->Bind(10);
		m_MeshRenderer.DefaultTexture()->Bind(11);
		m_MeshRenderer.DefaultTexture()->Bind(12);

		BindGeometryDescriptors(shadowTex);
		m_GeometryDescriptorSet->Apply(0);

		cmd.BindPipeline(m_CubePipeline);
		cmd.BindVertexBuffer(m_CubeVB, 0);
		cmd.BindIndexBuffer(m_CubeIB);
		cmd.BindDescriptorSet(m_GeometryDescriptorSet, 0);

		glm::mat4 model = scale(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 1.0f));
		PushDrawModel(cmd, model, model);
		cmd.DrawIndexed(36);

		glm::mat4 floorModel = translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, 0.0f));
		floorModel = scale(floorModel, glm::vec3(10.0f, 0.05f, 10.0f));
		PushDrawModel(cmd, floorModel, floorModel);
		cmd.DrawIndexed(36);
		m_CubePipeline->Unbind();
	}

	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	ForwardMeshRenderer m_MeshRenderer;
	Ref<RHIShader> m_LitShader;
	Ref<RHIDescriptorSet> m_GeometryDescriptorSet;
	Ref<RHIBuffer> m_CubeVB;
	Ref<RHIBuffer> m_CubeIB;
	Ref<RHIPipeline> m_CubePipeline;
	VertexLayout m_CubeVertexLayout;
	PipelineDesc m_CubePipelineDesc;
	RGTextureHandle m_ShadowMap;
	RGTextureHandle m_SceneColor;
	RGTextureHandle m_Velocity;
	RGTextureHandle m_Depth;
	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;
	uint32_t m_SampleCount = 4;
};

REGISTER_RENDER_PASS(GeometryPass, "Geometry", "Base", 10, true)
