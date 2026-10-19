#include<Pipeline/Passes/PassCommon.h>
#include<Pipeline/RenderPassRegistry.h>
#include<Platform/RenderAPIConfig.h>
#include<array>
#ifdef RenderAPI_OpenGL
#include<GL/glew.h>
#endif



class ShadowMapPass :public RenderGraphPass {
public:
	static constexpr uint32_t kMaxShadowSlots = RGFrameData::MaxShadowSlots;
	// 256-byte stride satisfies both Vulkan minUniformBufferOffsetAlignment and
	// OpenGL GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT for dynamic UBO bindings.
	static constexpr uint32_t kViewUBOStride = 256;

	ShadowMapPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData) {


		m_LightViewProjUBO = RHIBuffer::Create(BufferDesc{ kMaxShadowSlots * kViewUBOStride, BufferUsage::Uniform, true, nullptr });
		m_FrameUBO = RHIBuffer::Create(BufferDesc{ sizeof(FramData), BufferUsage::Uniform, true, nullptr });

		m_ShadowMapDepthShader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/ShadowMapDepth.shader");

		
		//unsigned char black[4] = { 0, 0, 0, 0 };

		//m_DefaultTex=RHITexture2D::Create({1,1,Format::RGBA8_UNORM,FilterMode::Linear,FilterMode::Linear,WrapMode::ClampToEdge,WrapMode::ClampToEdge,false,black});
		
		
		m_ShdowPassDescriptSet = RHIDescriptorSet::Create();

		if (m_ShdowPassDescriptSet){
			m_ShdowPassDescriptSet->BindUniformBuffer(0, m_LightViewProjUBO, sizeof(ViewUBO));
			m_ShdowPassDescriptSet->MarkBindingAsDynamic(0);
			//m_ShdowPassDescriptSet->BindTexture(10, m_DefaultTex, 10);
		}
		else {
			Error_Core("[ShadowMapPass]:m_ShdowPassDescriptSet is Null");
		}
	}
	const char* GetName() const override { return "ShadowMap"; }


	void Setup(RenderGraphBuilder& builder) override{
		RDGTextureDesc depthDesc;
		depthDesc.width = m_ShadowSize;
		depthDesc.height = m_ShadowSize;
		depthDesc.format = Format::D24_UNORM_S8_UINT;
		depthDesc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled;
		m_ShadowMapDepth = builder.CreateTexture("Depth", depthDesc);

		RDGTextureDesc visDesc;
		visDesc.width = m_ShadowSize;
		visDesc.height = m_ShadowSize;
		visDesc.format = Format::RGBA8_UNORM;
		visDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
		visDesc.minFilter = FilterMode::Nearest;
		visDesc.magFilter = FilterMode::Nearest;
		m_ShadowVis = builder.CreateTexture("ShadowVis", visDesc);

		builder.SetColorOutput(0, m_ShadowVis, RGLoadOp::Clear, { 1.0f, 1.0f, 1.0f, 1.0f });
		builder.SetDepthOutput(m_ShadowMapDepth, RGLoadOp::Clear, 1.0f);
		builder.Export(m_ShadowMapDepth);
		builder.Export(m_ShadowVis);

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

			m_ScenePipelineDesc.descriptorSets = { m_ShdowPassDescriptSet };
			PipelineDesc scenePipeDesc;
			scenePipeDesc.descriptorSets = { m_ShdowPassDescriptSet };
			scenePipeDesc.vertexLayout = sceneLayout;
			scenePipeDesc.pushConstantSize = sizeof(DrawPushConstants);
			scenePipeDesc.shader = m_ShadowMapDepthShader;
			scenePipeDesc.topology = PrimitiveTopology::Triangles;
			scenePipeDesc.cullMode = CullMode::None;
			scenePipeDesc.depthTest = true;
			scenePipeDesc.depthWrite = true;
			scenePipeDesc.srcBlend = BlendFactor::One;
			scenePipeDesc.dstBlend = BlendFactor::Zero;

			m_ScenePipeline = builder.CreatePipeline(m_ShadowMapDepthShader, sceneLayout, &scenePipeDesc);
		}
	}


	void Execute(RenderGraphContext& context)override
	{
		auto& cmd = context.GetCmd();

		if (m_FrameData)
		{
			m_FrameData->HasShadowMap = false;
			m_FrameData->ShadowSlots.clear();
			m_FrameData->LightShadowSlot.clear();
		}

		cmd.SetDepthTest(true);
		cmd.SetDepthFunc(CompareOp::Less);

#ifdef RenderAPI_OpenGL
		// Match Vulkan: shadow depth is 0 at the near plane and 1 at the far plane.
		glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
		glEnable(GL_SCISSOR_TEST);
#endif

		std::vector<glm::mat4> lightViewProjs;
		lightViewProjs.reserve(kMaxShadowSlots);

		for (auto [lightID, transform, light] : m_Scene->m_Registry.view<Component::Transform, Component::Light>().each())
		{
			const uint32_t entityKey = static_cast<uint32_t>(lightID);
			if (lightViewProjs.size() >= kMaxShadowSlots)
			{
				if (m_FrameData)
					m_FrameData->LightShadowSlot[entityKey] = -1;
				continue;
			}

			if (light.type == Light::LightType::Spot)
			{
				if (m_FrameData)
					m_FrameData->LightShadowSlot[entityKey] = static_cast<int32_t>(lightViewProjs.size());

				lightViewProjs.push_back(BuildSpotViewProj(
					transform.Position,
					light.Direction,
					light.Params.Spot.OuterAngle,
					light.Params.Spot.Radius));
			}
			else if (light.type == Light::LightType::Point)
			{
				if (lightViewProjs.size() + 6 > kMaxShadowSlots)
				{
					if (m_FrameData)
						m_FrameData->LightShadowSlot[entityKey] = -1;
					continue;
				}

				if (m_FrameData)
					m_FrameData->LightShadowSlot[entityKey] = static_cast<int32_t>(lightViewProjs.size());

				std::array<glm::mat4, 6> pointVPs{};
				BuildPointViewProjs(
					transform.Position,
					light.Params.Attenuation.Radius,
					pointVPs);
				for (const glm::mat4& vp : pointVPs)
					lightViewProjs.push_back(vp);
			}
			else
			{
				// Directional (handled by CSM) and Area are skipped here.
				if (m_FrameData)
					m_FrameData->LightShadowSlot[entityKey] = -1;
			}
		}

		if (lightViewProjs.empty())
		{
#ifdef RenderAPI_OpenGL
			glDisable(GL_SCISSOR_TEST);
			glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
#endif
			return;
		}

		const uint32_t slotCount = static_cast<uint32_t>(lightViewProjs.size());
		uint32_t tilesPerRow = 1;
		while (tilesPerRow * tilesPerRow < slotCount && tilesPerRow < 4)
			++tilesPerRow;
		const uint32_t tileSize = m_ShadowSize / tilesPerRow;
		const float invAtlasSize = 1.0f / static_cast<float>(m_ShadowSize);

		for (uint32_t slot = 0; slot < slotCount; ++slot)
		{
			ViewUBO vbo{ lightViewProjs[slot] };
			m_LightViewProjUBO->Upload(&vbo, sizeof(ViewUBO), slot * kViewUBOStride);
		}

		m_ShadowMapDepthShader->Bind();
		if (m_ShdowPassDescriptSet)
		{
			m_ShdowPassDescriptSet->Reset();
			m_ShdowPassDescriptSet->BindUniformBuffer(0, m_LightViewProjUBO, sizeof(ViewUBO));
			m_ShdowPassDescriptSet->Apply(0);
		}

		if (m_ScenePipeline)
		{
			cmd.BindPipeline(m_ScenePipeline);
		}

		for (uint32_t slot = 0; slot < slotCount; ++slot)
		{
			const uint32_t tileX = (slot % tilesPerRow) * tileSize;
			const uint32_t tileY = (slot / tilesPerRow) * tileSize;

			Viewport vp{};
			vp.x = static_cast<float>(tileX);
			vp.y = static_cast<float>(tileY);
			vp.width = static_cast<float>(tileSize);
			vp.height = static_cast<float>(tileSize);
			vp.minDepth = 0.0f;
			vp.maxDepth = 1.0f;
			cmd.SetViewport(vp);

			Scissor sc{};
			sc.x = static_cast<int32_t>(tileX);
			sc.y = static_cast<int32_t>(tileY);
			sc.width = tileSize;
			sc.height = tileSize;
			cmd.SetScissor(sc);

			if (m_FrameData)
			{
				RGShadowSlot slotInfo;
				slotInfo.ViewProj = lightViewProjs[slot];
				slotInfo.AtlasUV = glm::vec4(
					static_cast<float>(tileSize) * invAtlasSize,
					static_cast<float>(tileSize) * invAtlasSize,
					static_cast<float>(tileX) * invAtlasSize,
					static_cast<float>(tileY) * invAtlasSize);
				m_FrameData->ShadowSlots.push_back(slotInfo);
				m_FrameData->HasShadowMap = true;
			}

			const uint32_t dynamicOffset = slot * kViewUBOStride;

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

					Ref<Material> mat = i < meshrender.materials.size()
						? meshrender.materials[i]
						: meshrender.materials[0];

					mat->Render(m_ShadowMapDepthShader);

					cmd.BindDescriptorSet(m_ShdowPassDescriptSet, 0, &dynamicOffset, 1);
					PushDrawModel(cmd, modelMat, modelMat);

					mesh.gpuMesh->Draw(cmd);
				}
			}
		}

		cmd.SetViewport({ 0.0f, 0.0f, static_cast<float>(m_ShadowSize), static_cast<float>(m_ShadowSize), 0.0f, 1.0f });
		cmd.SetScissor({ 0, 0, m_ShadowSize, m_ShadowSize });

#ifdef RenderAPI_OpenGL
		glDisable(GL_SCISSOR_TEST);
		glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
#endif
	}


	void SetViewportSize(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;
	}

private:
	/*
	// Ortho box around the camera view, out to m_ShadowDistance. The box is the
	// frustum's bounding sphere, snapped to texels so the map follows the camera
	// without shimmering. (Kept for upcoming CSM pass)
	glm::mat4 BuildDirectionalViewProj(const glm::vec3& direction) const
	{
		const glm::vec3 up = (glm::abs(direction.y) > 0.99f)
			? glm::vec3(0.0f, 0.0f, 1.0f)
			: glm::vec3(0.0f, 1.0f, 0.0f);

		glm::vec3 center(0.0f);
		float radius = 20.0f;
		if (currentcamera)
		{
			const glm::mat4 invViewProj = glm::inverse(
				currentcamera->GetProj() * currentcamera->GetViewFront());
			const glm::vec3 camPos = currentcamera->getpos();

			glm::vec3 corners[8];
			int index = 0;
			for (int x = 0; x < 2; ++x)
			for (int y = 0; y < 2; ++y)
			for (int z = 0; z < 2; ++z)
			{
				const glm::vec4 clip(x * 2.0f - 1.0f, y * 2.0f - 1.0f, z * 2.0f - 1.0f, 1.0f);
				const glm::vec4 world = invViewProj * clip;
				corners[index++] = glm::vec3(world) / world.w;
			}

			glm::vec3 fit[8];
			for (int i = 0; i < 4; ++i)
			{
				glm::vec3 nearCorner = corners[i * 2];
				glm::vec3 farCorner = corners[i * 2 + 1];
				if (glm::length(nearCorner - camPos) > glm::length(farCorner - camPos))
				{
					const glm::vec3 swap = nearCorner;
					nearCorner = farCorner;
					farCorner = swap;
				}

				const float nearDist = glm::length(nearCorner - camPos);
				const float farDist = glm::length(farCorner - camPos);
				const float keep = glm::clamp(m_ShadowDistance, nearDist, farDist);
				const float span = glm::max(farDist - nearDist, 0.0001f);
				fit[i * 2] = nearCorner;
				fit[i * 2 + 1] = glm::mix(nearCorner, farCorner, (keep - nearDist) / span);
			}

			center = glm::vec3(0.0f);
			for (const glm::vec3& corner : fit)
				center += corner;
			center /= 8.0f;

			radius = 1.0f;
			for (const glm::vec3& corner : fit)
				radius = glm::max(radius, glm::length(corner - center));
		}

		const float texel = (radius * 2.0f) / static_cast<float>(m_ShadowSize);
		const glm::mat4 rotation = glm::lookAt(glm::vec3(0.0f), direction, up);
		glm::vec3 snapped = glm::vec3(rotation * glm::vec4(center, 1.0f));
		snapped.x = glm::floor(snapped.x / texel) * texel;
		snapped.y = glm::floor(snapped.y / texel) * texel;
		center = glm::vec3(glm::inverse(rotation) * glm::vec4(snapped, 1.0f));

		const float eyeDist = radius + m_CasterMargin;
		const float zFar = eyeDist + radius + 1.0f;
		const glm::vec3 eye = center - direction * eyeDist;
		const glm::mat4 lightView = glm::lookAt(eye, center, up);
		// Both backends store this map in [0, 1]. glm::ortho emits [-1, 1];
		// OpenGL reaches [0, 1] via glClipControl for the duration of this pass.
		glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.1f, zFar);
		lightProj[2][2] *= 0.5f;
		lightProj[3][2] = lightProj[3][2] * 0.5f + 0.5f;
		return lightProj * lightView;
	}
	*/

	glm::mat4 BuildSpotViewProj(
		const glm::vec3& position,
		const glm::vec3& direction,
		float outerAngleDeg,
		float radius) const
	{
		const glm::vec3 dir = glm::length(direction) > 0.0001f
			? glm::normalize(direction)
			: glm::vec3(0.0f, -1.0f, 0.0f);
		const glm::vec3 up = (glm::abs(dir.y) > 0.99f)
			? glm::vec3(0.0f, 0.0f, 1.0f)
			: glm::vec3(0.0f, 1.0f, 0.0f);

		const float fovDeg = glm::clamp(outerAngleDeg * 2.0f, 1.0f, 175.0f);
		const float zNear = 0.1f;
		const float zFar = glm::max(radius, zNear + 0.1f);

		const glm::mat4 lightView = glm::lookAt(position, position + dir, up);
		const glm::mat4 lightProj = glm::perspectiveRH_ZO(glm::radians(fovDeg), 1.0f, zNear, zFar);
		return lightProj * lightView;
	}

	void BuildPointViewProjs(
		const glm::vec3& position,
		float radius,
		std::array<glm::mat4, 6>& outViewProjs) const
	{
		struct FaceBasis
		{
			glm::vec3 targetOffset;
			glm::vec3 up;
		};
		// +X, -X, +Y, -Y, +Z, -Z
		static constexpr FaceBasis kFaces[6] = {
			{ glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f) },
			{ glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f) },
			{ glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f, 0.0f, -1.0f) },
			{ glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f, 0.0f,  1.0f) },
			{ glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, 1.0f,  0.0f) },
			{ glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, 1.0f,  0.0f) },
		};

		const float zNear = 0.1f;
		const float zFar = glm::max(radius, zNear + 0.1f);
		const glm::mat4 lightProj = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, zNear, zFar);

		for (size_t face = 0; face < 6; ++face)
		{
			const glm::mat4 lightView = glm::lookAt(
				position,
				position + kFaces[face].targetOffset,
				kFaces[face].up);
			outViewProjs[face] = lightProj * lightView;
		}
	}

public:

	struct ViewUBO {
		glm::mat4 LightviewProj;
	};


	struct FramData {
		glm::mat4 model_mat;
	};

	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	Ref<RHIBuffer> m_LightViewProjUBO;




	RGTextureHandle m_Depth;
	Ref<RHIBuffer> m_DrawUBO;
	Ref<RHIBuffer> m_FrameUBO;
	Ref<RHIDescriptorSet> m_ShdowPassDescriptSet;
	Ref<RHITexture2D> m_DefaultTex;
	RGTextureHandle m_ShadowMapDepth;
	RGTextureHandle m_ShadowVis;

	Ref<RHIPipeline> m_ScenePipeline;


	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;
	uint32_t m_ShadowSize = 2048;
	float m_ShadowDistance = 50.0f;
	float m_CasterMargin = 40.0f;

	int m_FrameCount = 0;
	glm::mat4 m_PrevViewProjMatrix = glm::mat4(1.0f);
	Ref<RHIShader> m_ShadowMapDepthShader;

	PipelineDesc m_ScenePipelineDesc;
};
REGISTER_RENDER_PASS(ShadowMapPass, "ShadowMap", "Base", 10, true)