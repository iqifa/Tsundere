#pragma once
#include "Pipeline/Passes/PassCommon.h"
#include "Scene/Vertex.h"
#include <cstddef>
#include <algorithm>
#include <unordered_map>

// Draws material shaders using the selected pass interface.
class ForwardMeshRenderer
{
public:
	struct Textures
	{
		RHITexture2D* shadow = nullptr;
		RHITexture2D* opaqueColor = nullptr;
		RHITexture2D* opaqueDepth = nullptr;
	};

	explicit ForwardMeshRenderer(bool gbuffer = false) : m_GBuffer(gbuffer)
	{
		m_CameraUBO = RHIBuffer::Create(BufferDesc{ sizeof(CameraUBO), BufferUsage::Uniform, true, nullptr });
		if (m_GBuffer)
		{
			const GBufferDrawUBO withoutNormal{ 0, {} };
			const GBufferDrawUBO withNormal{ 1, {} };
			m_FrameUBO = RHIBuffer::Create(BufferDesc{
				sizeof(GBufferDrawUBO), BufferUsage::Uniform, true, &withoutNormal });
			m_NormalMapUBO = RHIBuffer::Create(BufferDesc{
				sizeof(GBufferDrawUBO), BufferUsage::Uniform, true, &withNormal });
		}
		else
			m_FrameUBO = RHIBuffer::Create(BufferDesc{ sizeof(GeometryFrameUBO), BufferUsage::Uniform, true, nullptr });

		// 创建默认纹理
		unsigned char white[4] = { 255, 255, 255, 255 };
		m_DefaultTex = RHITexture2D::Create({ 1, 1, Format::RGBA8_UNORM, FilterMode::Linear, FilterMode::Linear,
											 WrapMode::ClampToEdge, WrapMode::ClampToEdge, false, white });
	}

	void ConfigurePipeline(PipelineDesc desc)
	{
		desc.vertexLayout.stride = sizeof(Vertex);
		desc.vertexLayout.attributes = {
			{ 0, VertexFormat::Float3, offsetof(Vertex, Position) },
			{ 1, VertexFormat::Float3, offsetof(Vertex, Normal) },
			{ 2, VertexFormat::Float2, offsetof(Vertex, TexCoords) },
			{ 3, VertexFormat::Float3, offsetof(Vertex, Tangent) },
			{ 4, VertexFormat::Float3, offsetof(Vertex, Bitangent) },
			{ 5, VertexFormat::Int4,   offsetof(Vertex, m_BoneIDs) },
			{ 6, VertexFormat::Float4, offsetof(Vertex, m_Weights) },
		};
		if (m_PipelineDesc.renderingSignature.sampleCount != desc.renderingSignature.sampleCount ||
			m_PipelineDesc.renderingSignature.colorFormats != desc.renderingSignature.colorFormats ||
			m_PipelineDesc.renderingSignature.depthFormat != desc.renderingSignature.depthFormat)
		{
			m_ShaderStates.clear();
			m_RejectedShaders.clear();
		}
		m_PipelineDesc = std::move(desc);
	}

	void UploadFrame(Scene& scene, const RGFrameData& frameData, RHITexture2D* shadowTex)
	{
		const auto& frameCamera = frameData.Camera;
		CameraUBO camera;
		camera.jitteredViewProj = frameCamera.JitteredViewProj;
		camera.viewProj = frameCamera.ViewProj;
		camera.prevViewProj = frameCamera.PrevViewProj;
		m_CameraUBO->Upload(&camera, sizeof(camera));
		if (m_GBuffer)
			return;
		glm::vec3 lightDir = glm::vec3(-0.5f, -1.0f, -0.5f);
		glm::vec3 lightColor = glm::vec3(1.0f);
		float ambientStrength = 0.1f;
		glm::vec3 viewPos = currentcamera->getpos();

		for (auto entityID : scene.m_Registry.view<Component::Light>())
		{
			auto& dl = scene.m_Registry.get<Component::Light>(entityID);
			if (dl.type != Component::Light::LightType::Directional)
				continue;
			lightDir = dl.Direction;
			lightColor = dl.Color * dl.Intensity;
			ambientStrength = dl.Ambient;
			break;
		}

		GeometryFrameUBO fubo;
		fubo.lightDir = glm::vec4(lightDir, 0.0f);
		fubo.lightColor = glm::vec4(lightColor, 0.0f);
		fubo.ambientStrength = ambientStrength;
		fubo.viewPos = glm::vec4(viewPos, 0.0f);
		fubo.u_LightViewProj = (shadowTex && !frameData.ShadowSlots.empty())
			? frameData.ShadowSlots[0].ViewProj
			: glm::mat4(1.0f);
		fubo.u_ShadowMapEnabled = shadowTex ? 1 : 0;
		fubo.u_ShadowMapSize = glm::vec2(2048.0f, 2048.0f);
		fubo.u_LightSize = 50.0f;
		fubo.hasNormalMap = 0;
		m_FrameUBO->Upload(&fubo, sizeof(fubo));
	}

	void BindFrameDescriptors(RHIDescriptorSet& descriptors) const
	{
		descriptors.BindUniformBuffer(1, m_FrameUBO);
		descriptors.BindUniformBuffer(2, m_CameraUBO);
	}

	const Ref<RHITexture2D>& DefaultTexture() const { return m_DefaultTex; }

	void Draw(RHICommandBuffer& cmd, RHIMesh& mesh, const Ref<Engine::Material>& material,
		const glm::mat4& model, const glm::mat4& prevModel, const Textures& textures)
	{
		ShaderState* state = GetShaderState(material->GetShader());
		if (!state)
			return;
		cmd.BindPipeline(state->pipeline);
		material->ApplyToShader(state->shader);
		BindMaterialDescriptors(*state, material, textures);
		if (state->hasDescriptors)
			cmd.BindDescriptorSet(state->descriptors, 0);
		if (!state->shader->GetReflection().PushConstants.empty())
			PushDrawModel(cmd, model, prevModel);
		mesh.Draw(cmd);
	}

	template<typename Visitor>
	static bool VisitReadyMeshes(Scene& scene, const RGFrameData& frameData, Visitor&& visit)
	{
		bool hadReadyMesh = false;
		for (auto [id, transform, renderer] : scene.m_Registry.view<Transform, MeshRender>().each())
		{
			if (renderer.ModelPath.empty() || renderer.materials.empty())
				continue;
			const auto model = My_map::GetModel(renderer.ModelPath);
			if (!model || model->meshes.empty())
				continue;

			const glm::mat4 current = transform.GetTransform();
			const glm::mat4 previous = frameData.PreviousModel(id, current);
			for (size_t i = 0; i < model->meshes.size(); ++i)
			{
				auto& mesh = model->meshes[i];
				if (!mesh.IsGPUReady())
					continue;
				hadReadyMesh = true;
				const auto& material = i < renderer.materials.size()
					? renderer.materials[i] : renderer.materials[0];
				if (material && material->GetShader())
					visit(mesh.gpuMesh, material, current, previous);
			}
		}
		return hadReadyMesh;
	}

private:
	struct GBufferDrawUBO
	{
		int32_t hasNormalMap;
		int32_t padding[3];
	};
	bool m_GBuffer;

	struct MaterialUBOEntry
	{
		Ref<RHIBuffer> buffer;
		std::vector<char> bytes;
	};

	struct MaterialBlockState
	{
		Ref<RHIBuffer> defaultMaterialUBO;
		std::unordered_map<const Engine::Material*, MaterialUBOEntry> materialUBOs;
		uint32_t materialBlockSize = 0;
	};

	struct ShaderState
	{
		Ref<RHIShader> shader;
		Ref<RHIPipeline> pipeline;
		Ref<RHIDescriptorSet> descriptors;
		std::unordered_map<uint32_t, MaterialBlockState> materialBlocks;
		bool hasDescriptors = false;
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
        const auto memberAt = [](const ShaderBlock* block, uint32_t offset, const char* type) {
            if (!block) return true;
            return std::any_of(block->Members.begin(), block->Members.end(), [&](const auto& member) {
                return member.Offset == offset && member.Type == type;
            });
        };
        const ShaderBlock* push = reflection.PushConstants.empty() ? nullptr : &reflection.PushConstants[0];
		const bool validLayout = (m_GBuffer ? (!frameBlock || frameBlock->Size == sizeof(GBufferDrawUBO))
			: (frameBlock && frameBlock->Size == sizeof(GeometryFrameUBO))) &&
			(m_GBuffer ? (!cameraBlock || cameraBlock->Size == sizeof(CameraUBO))
			 : (cameraBlock && cameraBlock->Size == sizeof(CameraUBO))) &&
			((m_GBuffer && reflection.PushConstants.empty()) ||
			 (!reflection.PushConstants.empty() && reflection.PushConstants[0].Size == sizeof(DrawPushConstants))) &&
			reflection.StorageBlocks.empty() && reflection.StorageImages.empty() &&
            memberAt(cameraBlock, 0, "mat4") && memberAt(cameraBlock, 64, "mat4") &&
            memberAt(cameraBlock, 128, "mat4") && memberAt(push, 0, "mat4") && memberAt(push, 64, "mat4") &&
            (!m_GBuffer || memberAt(frameBlock, 0, "int"));
		if (!validLayout)
		{
			Error_Core("ForwardMeshRenderer: material shader '{}' does not implement the pass camera/frame/push-constant interface", shader->GetPath());
			m_RejectedShaders.emplace(shader.get(), shader);
			return nullptr;
		}

		ShaderState state;
		state.shader = shader;
		state.hasDescriptors = !reflection.UniformBlocks.empty() || !reflection.SampledImages.empty();
		state.descriptors = RHIDescriptorSet::Create();
		if (!state.descriptors)
			return nullptr;
		BindDeclaredFrameDescriptors(state);
		for (const auto& block : reflection.UniformBlocks)
		{
			if (block.Set == 0 && (block.Binding == 1 || block.Binding == 2))
				continue;
			if (block.Set != 0 || block.Binding < 3 || block.Size == 0)
			{
				Error_Core("ForwardMeshRenderer: material shader '{}' has an unsupported uniform block binding", shader->GetPath());
				m_RejectedShaders.emplace(shader.get(), shader);
				return nullptr;
			}
			auto& materialBlock = state.materialBlocks[block.Binding];
			materialBlock.materialBlockSize = block.Size;
			std::vector<char> zero(block.Size, 0);
			materialBlock.defaultMaterialUBO = RHIBuffer::Create(BufferDesc{
				block.Size, BufferUsage::Uniform, true, zero.data() });
			state.descriptors->BindUniformBuffer(block.Binding, materialBlock.defaultMaterialUBO);
		}
		for (const auto& image : reflection.SampledImages)
		{
			if (image.Set != 0 || image.Binding < 10 || image.Type != "sampler2D" ||
                reflection.FindUniformBlock(image.Binding) ||
				(!reflection.Transparent && IsSceneTexture(image.Name)))
			{
				Error_Core("ForwardMeshRenderer: material shader '{}' has an unsupported sampler", shader->GetPath());
				m_RejectedShaders.emplace(shader.get(), shader);
				return nullptr;
			}
			state.descriptors->BindTexture(image.Binding, m_DefaultTex, image.Binding);
		}
		// Vulkan freezes the descriptor layout when the pipeline is created.
		state.descriptors->Apply(0);
		PipelineDesc desc = m_PipelineDesc;
		desc.shader = shader;
		desc.pushConstantSize = reflection.PushConstants.empty() ? 0 : sizeof(DrawPushConstants);
		desc.descriptorSets.clear();
		if (state.hasDescriptors) desc.descriptorSets.push_back(state.descriptors);
		state.pipeline = RHIPipeline::Create(desc);
		if (!state.pipeline)
			return nullptr;
		auto [it, inserted] = m_ShaderStates.emplace(shader.get(), std::move(state));
		return &it->second;
	}

	Ref<RHIBuffer> AcquireMaterialUBO(ShaderState& shaderState, uint32_t binding, const Ref<Engine::Material>& mat)
	{
		auto& state = shaderState.materialBlocks.at(binding);
		if (state.materialBlockSize == 0)
			return nullptr;
		std::vector<char> packed;
		if (!mat || mat->PackMaterialBlock(shaderState.shader->GetReflection(), packed, binding) != state.materialBlockSize)
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

    void BindDeclaredFrameDescriptors(ShaderState& state) const
    {
        const auto& reflection = state.shader->GetReflection();
        if (reflection.FindUniformBlock(1)) state.descriptors->BindUniformBuffer(1, m_FrameUBO);
        if (reflection.FindUniformBlock(2)) state.descriptors->BindUniformBuffer(2, m_CameraUBO);
    }

	void BindMaterialDescriptors(ShaderState& state, const Ref<Engine::Material>& mat,
		const Textures& textures)
	{
		state.descriptors->Reset();
		BindDeclaredFrameDescriptors(state);
		if (m_GBuffer)
			for (const auto& image : state.shader->GetReflection().SampledImages)
				if (state.shader->GetReflection().FindUniformBlock(1) &&
					image.Name == "texture_normal1" && mat->GetTexture(image.Name))
					state.descriptors->BindUniformBuffer(1, m_NormalMapUBO);
		for (const auto& [binding, block] : state.materialBlocks)
			state.descriptors->BindUniformBuffer(binding, AcquireMaterialUBO(state, binding, mat));
		for (const auto& image : state.shader->GetReflection().SampledImages)
		{
			RHITexture2D* texture = image.Name == "u_ShadowMap" && textures.shadow
				? textures.shadow : nullptr;
			if (image.Name == "u_OpaqueColor")
				texture = textures.opaqueColor;
			else if (image.Name == "u_OpaqueDepth")
				texture = textures.opaqueDepth;
			Ref<RHITexture2D> materialTexture = mat->GetTexture(image.Name);
			if (!texture && materialTexture && !IsSceneTexture(image.Name))
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

	Ref<RHIBuffer> m_CameraUBO;
	Ref<RHIBuffer> m_FrameUBO;
	Ref<RHIBuffer> m_NormalMapUBO;
	Ref<RHITexture2D> m_DefaultTex;
	PipelineDesc m_PipelineDesc;
	std::unordered_map<const RHIShader*, ShaderState> m_ShaderStates;
	std::unordered_map<const RHIShader*, Ref<RHIShader>> m_RejectedShaders;
};
