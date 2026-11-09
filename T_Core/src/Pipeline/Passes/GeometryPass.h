#pragma once
#include "Pipeline/RenderGraphPass.h"
#include "Pipeline/RenderPassRegistry.h"
#include "Pipeline/Passes/PassCommon.h"
#include "Scene/Scene.h"
#include "Scene/Vertex.h"
#include <cstddef>
#include <unordered_map>

// Forward pass: each scene mesh draws with its material shader. Compatible
// shaders use frame UBO binding 1, camera UBO binding 2, model/prevModel push
// constants, color output 0 and velocity output 1. Material UBO binding 3
// and sampler2D bindings >= 10 are optional.
class GeometryPass : public RenderGraphPass
{
public:
	GeometryPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData)
	{
		// 初始化 Shader 和资源
		m_LitShader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/Lit.shader");

		m_CameraUBO = RHIBuffer::Create(BufferDesc{ sizeof(CameraUBO), BufferUsage::Uniform, true, nullptr });
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
			m_GeometryDescriptorSet->BindUniformBuffer(1, m_FrameUBO);
			m_GeometryDescriptorSet->BindUniformBuffer(2, m_CameraUBO);
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
			m_CubePipelineDesc.descriptorSets = { m_GeometryDescriptorSet };
			m_CubePipeline = builder.CreatePipeline(
				m_LitShader, m_CubeVertexLayout, &m_CubePipelineDesc);
			if (m_CubePipeline)
			{
				m_CubePipeline->SetupVertexFormat(m_CubeVB);
				m_CubePipeline->SetupIndexBuffer(m_CubeIB);
			}
		}

		// Material pipelines can be created when a material or its shader changes,
		// without rebuilding the graph. They all use this pass's attachment signature.
		m_ScenePipelineDesc = m_CubePipelineDesc;
		m_ScenePipelineDesc.vertexLayout.stride = sizeof(Vertex);
		m_ScenePipelineDesc.vertexLayout.attributes = {
			{ 0, VertexFormat::Float3, offsetof(Vertex, Position) },
			{ 1, VertexFormat::Float3, offsetof(Vertex, Normal) },
			{ 2, VertexFormat::Float2, offsetof(Vertex, TexCoords) },
			{ 3, VertexFormat::Float3, offsetof(Vertex, Tangent) },
			{ 4, VertexFormat::Float3, offsetof(Vertex, Bitangent) },
			{ 5, VertexFormat::Int4,   offsetof(Vertex, m_BoneIDs) },
			{ 6, VertexFormat::Float4, offsetof(Vertex, m_Weights) },
		};
		m_ScenePipelineDesc.renderingSignature.colorFormats = { Format::RGBA8_UNORM, Format::RG16F };
		m_ScenePipelineDesc.renderingSignature.depthFormat = Format::D24_UNORM_S8_UINT;
		m_ScenePipelineDesc.renderingSignature.sampleCount = m_SampleCount;
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
		const auto& frameCamera = m_FrameData->Camera;

		// 4. 获取光照信息
		glm::vec3 lightDir = glm::vec3(-0.5f, -1.0f, -0.5f);
		glm::vec3 lightColor = glm::vec3(1.0f);
		float ambientStrength = 0.1f;
		glm::vec3 viewPos = currentcamera->getpos();

		for (auto entityID : m_Scene->m_Registry.view<Component::Light>())
		{
			auto& dl = m_Scene->m_Registry.get<Component::Light>(entityID);
			if (dl.type != Component::Light::LightType::Directional)
				continue;
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
		fubo.u_LightViewProj = (shadowTex && m_FrameData && !m_FrameData->ShadowSlots.empty())
			? m_FrameData->ShadowSlots[0].ViewProj
			: glm::mat4(1.0f);
		fubo.u_ShadowMapEnabled = shadowTex ? 1 : 0;
		fubo.u_ShadowMapSize = glm::vec2(2048.0f, 2048.0f);
		fubo.u_LightSize = 50.0f;
		fubo.hasNormalMap = 0;
		m_FrameUBO->Upload(&fubo, sizeof(fubo));

		CameraUBO camera;
		camera.jitteredViewProj = frameCamera.JitteredViewProj;
		camera.viewProj = frameCamera.ViewProj;
		camera.prevViewProj = frameCamera.PrevViewProj;
		m_CameraUBO->Upload(&camera, sizeof(camera));

		// 7. 绑定 Shader 和稳定的 descriptor layout
		// 9. 绘制场景
		bool hadReadyMesh = false;

		for (auto [entityID, transform, meshrender] : m_Scene->m_Registry.view<Component::Transform, Component::MeshRender>().each())
		{
			if (meshrender.ModelPath.empty() || meshrender.materials.empty())
				continue;

			Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
			if (!model || model->meshes.empty())
				continue;

			glm::mat4 modelMat = transform.GetTransform();

			for (size_t i = 0; i < model->meshes.size(); i++)
			{
				auto& mesh = model->meshes[i];
				if (!mesh.IsGPUReady())
					continue;
				hadReadyMesh = true;

				Ref<Engine::Material> mat = i < meshrender.materials.size()
					? meshrender.materials[i]
					: meshrender.materials[0];
				// Scene meshes use their material's shader. A missing shader is an
				// invalid material, not a request to substitute the pass shader.
				if (!mat || !mat->GetShader())
					continue;
				ShaderState* state = GetShaderState(mat->GetShader());
				if (!state)
					continue;

				cmd.BindPipeline(state->pipeline);
				mat->ApplyToShader(state->shader);
				BindMaterialDescriptors(*state, mat, shadowTex);
				cmd.BindDescriptorSet(state->descriptors, 0);
				PushDrawModel(cmd, modelMat, modelMat);

				mesh.gpuMesh->Draw(cmd);
			}
		}

		// 10. Fallback Cube（如果没有场景物体）
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

	// 1 = 关闭 MSAA；支持 2/4/8。sample count 会烘焙进 pipeline，修改后需要重建 RenderGraph。
	void SetSampleCount(uint32_t samples)
	{
		if (samples == m_SampleCount)
			return;
		m_SampleCount = samples;
		m_CubePipeline.reset();
		m_ShaderStates.clear();
		m_RejectedShaders.clear();
	}
	uint32_t GetSampleCount() const { return m_SampleCount; }

private:
	struct MaterialUBOEntry
	{
		Ref<RHIBuffer> buffer;
		std::vector<char> bytes;
	};

	struct ShaderState
	{
		Ref<RHIShader> shader;
		Ref<RHIPipeline> pipeline;
		Ref<RHIDescriptorSet> descriptors;
		Ref<RHIBuffer> defaultMaterialUBO;
		std::unordered_map<const Engine::Material*, MaterialUBOEntry> materialUBOs;
		uint32_t materialBlockSize = 0;
	};

	ShaderState* GetShaderState(const Ref<RHIShader>& shader)
	{
		if (auto it = m_ShaderStates.find(shader.get()); it != m_ShaderStates.end())
			return &it->second;
		if (m_RejectedShaders.find(shader.get()) != m_RejectedShaders.end())
			return nullptr;

		const ShaderReflection& reflection = shader->GetReflection();
		const ShaderBlock* frameBlock = reflection.FindUniformBlock(1);
		const ShaderBlock* cameraBlock = reflection.FindUniformBlock(2);
		const bool validLayout = frameBlock && frameBlock->Size == sizeof(GeometryFrameUBO) &&
			cameraBlock && cameraBlock->Size == sizeof(CameraUBO) &&
			!reflection.PushConstants.empty() &&
			reflection.PushConstants[0].Size >= sizeof(DrawPushConstants) &&
			reflection.StorageBlocks.empty() && reflection.StorageImages.empty();
		if (!validLayout)
		{
			Error_Core("GeometryPass: material shader '{}' does not implement the forward camera/frame/push-constant interface", shader->GetPath());
			m_RejectedShaders.emplace(shader.get(), shader);
			return nullptr;
		}

		ShaderState state;
		state.shader = shader;
		state.descriptors = RHIDescriptorSet::Create();
		if (!state.descriptors)
			return nullptr;
		state.descriptors->BindUniformBuffer(1, m_FrameUBO);
		state.descriptors->BindUniformBuffer(2, m_CameraUBO);
		for (const auto& block : reflection.UniformBlocks)
		{
			if (block.Set == 0 && (block.Binding == 1 || block.Binding == 2))
				continue;
			if (block.Set != 0 || block.Binding != 3 || block.Size == 0 ||
				state.materialBlockSize != 0)
			{
				Error_Core("GeometryPass: material shader '{}' has an unsupported uniform block binding", shader->GetPath());
				m_RejectedShaders.emplace(shader.get(), shader);
				return nullptr;
			}
			state.materialBlockSize = block.Size;
			std::vector<char> zero(block.Size, 0);
			state.defaultMaterialUBO = RHIBuffer::Create(BufferDesc{
				block.Size, BufferUsage::Uniform, true, zero.data() });
			state.descriptors->BindUniformBuffer(3, state.defaultMaterialUBO);
		}
		for (const auto& image : reflection.SampledImages)
		{
			if (image.Set != 0 || image.Binding < 10 || image.Type != "sampler2D")
			{
				Error_Core("GeometryPass: material shader '{}' has an unsupported sampler", shader->GetPath());
				m_RejectedShaders.emplace(shader.get(), shader);
				return nullptr;
			}
			state.descriptors->BindTexture(image.Binding, m_DefaultTex, image.Binding);
		}
		// Vulkan freezes the descriptor layout when the pipeline is created.
		state.descriptors->Apply(0);
		PipelineDesc desc = m_ScenePipelineDesc;
		desc.shader = shader;
		desc.descriptorSets = { state.descriptors };
		state.pipeline = RHIPipeline::Create(desc);
		if (!state.pipeline)
			return nullptr;
		auto [it, inserted] = m_ShaderStates.emplace(shader.get(), std::move(state));
		return &it->second;
	}

	Ref<RHIBuffer> AcquireMaterialUBO(ShaderState& state, const Ref<Engine::Material>& mat)
	{
		if (state.materialBlockSize == 0)
			return nullptr;
		std::vector<char> packed;
		if (!mat || mat->PackMaterialBlock(state.shader->GetReflection(), packed) != state.materialBlockSize)
			return state.defaultMaterialUBO;
		auto& entry = state.materialUBOs[mat.get()];
		if (!entry.buffer || entry.bytes != packed)
		{
			if (!entry.buffer)
				entry.buffer = RHIBuffer::Create(BufferDesc{
					state.materialBlockSize, BufferUsage::Uniform, true, packed.data() });
			else
				entry.buffer->Upload(packed.data(), state.materialBlockSize);
			entry.bytes = std::move(packed);
		}
		return entry.buffer;
	}

	void BindMaterialDescriptors(ShaderState& state, const Ref<Engine::Material>& mat,
		RHITexture2D* shadowTex)
	{
		state.descriptors->Reset();
		state.descriptors->BindUniformBuffer(1, m_FrameUBO);
		state.descriptors->BindUniformBuffer(2, m_CameraUBO);
		if (state.materialBlockSize != 0)
			state.descriptors->BindUniformBuffer(3, AcquireMaterialUBO(state, mat));
		for (const auto& image : state.shader->GetReflection().SampledImages)
		{
			RHITexture2D* texture = image.Name == "u_ShadowMap" && shadowTex
				? shadowTex : nullptr;
			Ref<RHITexture2D> materialTexture = mat->GetTexture(image.Name);
			if (!texture && materialTexture)
				texture = materialTexture.get();
			state.descriptors->BindTexture(image.Binding,
				texture ? texture : m_DefaultTex.get(), image.Binding);
		}
		state.descriptors->Apply(0);
	}

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
		m_GeometryDescriptorSet->BindUniformBuffer(1, m_FrameUBO);
		m_GeometryDescriptorSet->BindUniformBuffer(2, m_CameraUBO);
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
		pipeDesc.pushConstantSize = sizeof(DrawPushConstants);

		m_CubeVertexLayout = vtxLayout;
		m_CubePipelineDesc = pipeDesc;
	}

	void DrawFallbackCube(RHICommandBuffer& cmd, RHITexture2D* shadowTex)
	{
		m_LitShader->Bind();
		m_DefaultTex->Bind(10);
		m_DefaultTex->Bind(11);
		m_DefaultTex->Bind(12);

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
	Ref<RHIShader> m_LitShader;
	Ref<RHIBuffer> m_CameraUBO;
	Ref<RHIBuffer> m_FrameUBO;
	Ref<RHIDescriptorSet> m_GeometryDescriptorSet;
	Ref<RHITexture2D> m_DefaultTex;

	Ref<RHIBuffer> m_CubeVB;
	Ref<RHIBuffer> m_CubeIB;
	Ref<RHIPipeline> m_CubePipeline;
	PipelineDesc m_ScenePipelineDesc;
	std::unordered_map<const RHIShader*, ShaderState> m_ShaderStates;
	std::unordered_map<const RHIShader*, Ref<RHIShader>> m_RejectedShaders;
	VertexLayout m_CubeVertexLayout;
	PipelineDesc m_CubePipelineDesc;

	// 资源 Handle（Setup 时填充，Execute 时使用）
	RGTextureHandle m_ShadowMap;
	RGTextureHandle m_SceneColor;
	RGTextureHandle m_Velocity;
	RGTextureHandle m_Depth;

	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;
	uint32_t m_SampleCount = 4;
};

// 自动注册到 RenderPassRegistry
// 参数：类名, Pass名称, 分类, 优先级, 默认启用
REGISTER_RENDER_PASS(GeometryPass, "Geometry", "Base", 10, true)
