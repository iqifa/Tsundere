#pragma once
#include "Pipeline/RenderGraphPass.h"
#include "Pipeline/Passes/PassCommon.h"
#include "Scene/Scene.h"

// GeometryPassV2 - 新接口的 Geometry Pass
// 使用统一的 RenderGraphPass 基类，资源通过名字引用
class GeometryPassV2 : public RenderGraphPass
{
public:
	GeometryPassV2(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData)
	{
		// 初始化 Shader 和资源
		m_LitShader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/Lit.shader");

		// 创建 UBO
		m_DrawUBO = RHIBuffer::Create(BufferDesc{ sizeof(GeometryDrawUBO), BufferUsage::Uniform, true, nullptr });
		m_FrameUBO = RHIBuffer::Create(BufferDesc{ sizeof(GeometryFrameUBO), BufferUsage::Uniform, true, nullptr });

		// 创建 Fallback Cube
		InitFallbackGeometry();

		// 创建默认纹理
		unsigned char white[4] = { 255, 255, 255, 255 };
		m_DefaultTex = RHITexture2D::Create({ 1, 1, Format::RGBA8_UNORM, FilterMode::Linear, FilterMode::Linear,
											 WrapMode::ClampToEdge, WrapMode::ClampToEdge, false, white });

		m_GeometryDescriptorSet = RHIDescriptorSet::Create();
		if (m_GeometryDescriptorSet)
		{
			m_GeometryDescriptorSet->BindUniformBuffer(0, m_DrawUBO);
			m_GeometryDescriptorSet->BindUniformBuffer(1, m_FrameUBO);
			m_GeometryDescriptorSet->BindTexture(10, m_DefaultTex, 10);
			m_GeometryDescriptorSet->BindTexture(11, m_DefaultTex, 11);
			m_GeometryDescriptorSet->BindTexture(12, m_DefaultTex, 12);
			m_GeometryDescriptorSet->BindTexture(13, m_DefaultTex, 13);
		}
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
		colorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

		RDGTextureDesc velocityDesc = colorDesc;
		velocityDesc.format = Format::RG16F;

		RDGTextureDesc depthDesc;
		depthDesc.width = m_Width;
		depthDesc.height = m_Height;
		depthDesc.format = Format::D24_UNORM_S8_UINT;
		depthDesc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled;

		m_SceneColor = builder.CreateTexture("SceneColor", colorDesc);
		m_Velocity = builder.CreateTexture("Velocity", velocityDesc);
		m_Depth = builder.CreateTexture("Depth", depthDesc);

		// 3. 声明渲染目标（内部会注册 Write 依赖）
		builder.SetColorOutput(0, m_SceneColor, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 1.0f });
		builder.SetColorOutput(1, m_Velocity, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 0.0f });
		builder.SetDepthOutput(m_Depth, RGLoadOp::Clear, 1.0f);

		// 这些资源会在图执行结束后被 ExampleLayer 读取并显示。
		builder.Export(m_SceneColor);
		builder.Export(m_Velocity);

		// Derive the pipeline rendering signature after declaring all outputs.
		if (currentcamera && currentcamera->skybox)
			currentcamera->skybox->InitializePipeline(builder);
		if (!m_CubePipeline)
		{
			m_CubePipelineDesc.descriptorSets = { m_GeometryDescriptorSet };
			m_CubePipeline = builder.CreatePipeline(
				m_LitShader, m_CubeVertexLayout, &m_CubePipelineDesc);
			if (m_CubePipeline)
			{
				m_CubePipeline->SetupVertexFormat(m_CubeVB);
				m_CubePipeline->SetupIndexBuffer(m_CubeIB);
			}
		}
	}

	void Execute(RenderGraphContext& context) override
	{
		auto& cmd = context.GetCmd();

		// 1. 获取物理资源
		RHITexture2D* shadowTex = m_ShadowMap.id != InvalidResourceId
			? context.GetTexture(m_ShadowMap)
			: nullptr;

		// 2. 设置渲染状态
		cmd.SetDepthTest(true);
		cmd.SetDepthFunc(CompareOp::Less);

		// 3. 获取相机矩阵
		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 currentViewProj = proj * view;

		if (EnableJitter)
			proj = Jittering(proj, (float)m_Width, (float)m_Height);

		// 4. 获取光照信息
		vec3 lightDir = vec3(-0.5f, -1.0f, -0.5f);
		vec3 lightColor = vec3(1.0f);
		float ambientStrength = 0.1f;
		vec3 viewPos = currentcamera->getpos();

		for (auto entityID : m_Scene->m_Registry.view<Component::DirectionalLight>())
		{
			auto& dl = m_Scene->m_Registry.get<Component::DirectionalLight>(entityID);
			lightDir = dl.Direction;
			lightColor = dl.Color * dl.Intensity;
			ambientStrength = dl.Ambient;
			break;
		}

		// 5. 绘制 Skybox
		currentcamera->RenderSkyBox(cmd);

		// 6. 上传 Frame UBO
		GeometryFrameUBO fubo;
		fubo.lightDir = glm::vec4(lightDir, 0.0f);
		fubo.lightColor = glm::vec4(lightColor, 0.0f);
		fubo.ambientStrength = ambientStrength;
		fubo.viewPos = glm::vec4(viewPos, 0.0f);
		fubo.u_LightViewProj = shadowTex && m_FrameData
			? m_FrameData->ShadowLightViewProj
			: glm::mat4(1.0f);
		fubo.u_ShadowMapEnabled = shadowTex ? 1 : 0;
		fubo.u_ShadowMapSize = glm::vec2(2048.0f, 2048.0f);
		fubo.u_LightSize = 50.0f;
		fubo.hasNormalMap = 0;
		m_FrameUBO->Upload(&fubo, sizeof(fubo));

		// 7. 绑定 Shader 和稳定的 descriptor layout
		m_LitShader->Bind();

		if (!m_GeometryDescriptorSet)
			return;

		BindGeometryDescriptors(shadowTex);
		m_GeometryDescriptorSet->Apply(0);

		// 9. 绘制场景
		bool drewSomething = false;
		m_DefaultTex->Bind(0);

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

				mat->Render(m_LitShader);

				// Per-draw UBO
				GeometryDrawUBO dubo;
				dubo.MVP_matrix = proj * view * modelMat;
				dubo.model = modelMat;
				dubo.prevModel = modelMat;
				dubo.viewProj = currentViewProj;
				dubo.prevViewProj = m_PrevViewProjMatrix;
				m_DrawUBO->Upload(&dubo, sizeof(dubo));

				BindGeometryDescriptors(shadowTex);
				m_GeometryDescriptorSet->Apply(0);
				cmd.BindDescriptorSet(m_GeometryDescriptorSet, 0);

				mesh.gpuMesh->Draw(cmd);
				drewSomething = true;
			}
		}

		// 10. Fallback Cube（如果没有场景物体）
		if (!drewSomething)
		{
			DrawFallbackCube(cmd, proj, view, currentViewProj, shadowTex);
		}

		m_PrevViewProjMatrix = currentViewProj;
		m_FrameCount++;
	}

	bool EnableJitter = true;

	void SetViewportSize(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;
	}

private:
	// UBO 结构（和原 GeometryPass 一致）
	struct GeometryDrawUBO
	{
		glm::mat4 MVP_matrix;
		glm::mat4 model;
		glm::mat4 prevModel;
		glm::mat4 viewProj;
		glm::mat4 prevViewProj;
	};

	struct GeometryFrameUBO
	{
		glm::vec4 lightDir;
		glm::vec4 lightColor;
		float ambientStrength;
		float _pad0[3];
		glm::vec4 viewPos;
		glm::mat4 u_LightViewProj;
		int32_t hasNormalMap;
		int32_t u_ShadowMapEnabled;
		glm::vec2 u_ShadowMapSize;
		float u_LightSize;
		int32_t _pad1[3];
	};

	void BindGeometryDescriptors(RHITexture2D* shadowTex)
	{
		m_GeometryDescriptorSet->Reset();
		m_GeometryDescriptorSet->BindUniformBuffer(0, m_DrawUBO);
		m_GeometryDescriptorSet->BindUniformBuffer(1, m_FrameUBO);
		m_GeometryDescriptorSet->BindTexture(10, m_DefaultTex, 10);
		m_GeometryDescriptorSet->BindTexture(11, m_DefaultTex, 11);
		m_GeometryDescriptorSet->BindTexture(12, m_DefaultTex, 12);

		if (shadowTex)
			m_GeometryDescriptorSet->BindTexture(13, shadowTex, 13);
		else
			m_GeometryDescriptorSet->BindTexture(13, m_DefaultTex, 13);
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

		m_CubeVertexLayout = vtxLayout;
		m_CubePipelineDesc = pipeDesc;
	}

	void DrawFallbackCube(
		RHICommandBuffer& cmd,
		const mat4& proj,
		const mat4& view,
		const mat4& currentViewProj,
		RHITexture2D* shadowTex)
	{
		m_LitShader->Bind();
		m_DefaultTex->Bind(10);
		m_DefaultTex->Bind(11);
		m_DefaultTex->Bind(12);

		mat4 model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
		mat4 mvp = proj * view * model;

		GeometryDrawUBO dubo;
		dubo.MVP_matrix = mvp;
		dubo.model = model;
		dubo.prevModel = model;
		dubo.viewProj = currentViewProj;
		dubo.prevViewProj = m_PrevViewProjMatrix;
		m_DrawUBO->Upload(&dubo, sizeof(dubo));

		BindGeometryDescriptors(shadowTex);
		m_GeometryDescriptorSet->Apply(0);

		cmd.BindPipeline(m_CubePipeline);
		cmd.BindVertexBuffer(m_CubeVB, 0);
		cmd.BindIndexBuffer(m_CubeIB);
		cmd.BindDescriptorSet(m_GeometryDescriptorSet, 0);
		cmd.DrawIndexed(36);

		// Floor
		mat4 floorModel = translate(mat4(1.0f), vec3(0.0f, -2.0f, 0.0f));
		floorModel = scale(floorModel, vec3(10.0f, 0.05f, 10.0f));
		mat4 floorMVP = proj * view * floorModel;
		dubo.MVP_matrix = floorMVP;
		dubo.model = floorModel;
		dubo.prevModel = floorModel;
		m_DrawUBO->Upload(&dubo, sizeof(dubo));

		BindGeometryDescriptors(shadowTex);
		m_GeometryDescriptorSet->Apply(0);
		cmd.BindDescriptorSet(m_GeometryDescriptorSet, 0);
		cmd.DrawIndexed(36);
		m_CubePipeline->Unbind();
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

	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	Ref<RHIShader> m_LitShader;
	Ref<RHIBuffer> m_DrawUBO;
	Ref<RHIBuffer> m_FrameUBO;
	Ref<RHIDescriptorSet> m_GeometryDescriptorSet;
	Ref<RHITexture2D> m_DefaultTex;

	Ref<RHIBuffer> m_CubeVB;
	Ref<RHIBuffer> m_CubeIB;
	Ref<RHIPipeline> m_CubePipeline;
	VertexLayout m_CubeVertexLayout;
	PipelineDesc m_CubePipelineDesc;

	// 资源 Handle（Setup 时填充，Execute 时使用）
	RGTextureHandle m_ShadowMap;
	RGTextureHandle m_SceneColor;
	RGTextureHandle m_Velocity;
	RGTextureHandle m_Depth;

	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;
	int m_FrameCount = 0;
	mat4 m_PrevViewProjMatrix = mat4(1.0f);
};
