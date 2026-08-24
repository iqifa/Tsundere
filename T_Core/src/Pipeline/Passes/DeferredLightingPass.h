#pragma once
#include "Pipeline/Passes/PassCommon.h"
#include "Pipeline/Passes/DDGIPass.h"
#include "Pipeline/Passes/GBufferPass.h"   // AddToGraph takes GBufferPass::GraphOutputs

class  DeferredLightingPass : public RenderPass
{
public:
	int DebugMode = 0;
	bool DDGIEnabled = true;
	bool ClusteredLightingEnabled = true;
	int ClusterTileSize = 32;
	int ClusterZSlices = 16;
	int MaxLightsPerCluster = 64;
	int MaxLocalLights = 512;

	int GetLocalLightCount() const { return (int)m_GPULights.size(); }
	unsigned int GetClusterCountX() const { return m_ClusterCountX; }
	unsigned int GetClusterCountY() const { return m_ClusterCountY; }
	unsigned int GetClusterCountZ() const { return m_ClusterCountZ; }

	void SetDDGIPass(DDGIPass* ddgi) { m_DDGIPass = ddgi; }

	// --- RenderGraph path ---------------------------------------------------
	// Declares this pass into a graph and returns the lit scene color.
	//
	// Execute() sets 60+ uniforms across ~130 lines. Rather than duplicate that
	// into a lambda, this reuses it verbatim: the graph owns the render target
	// and binds it, m_GraphManagedTarget suppresses Execute()'s own FBO bind,
	// and a temporary RenderResources bridges graph handles to the raw GL IDs
	// Execute() already expects. One code path, two ways of being driven.
	//
	// ShadowMask and the DDGI atlases come from ShadowPass / DDGIPass, which are
	// compute + SSBO and not migrated yet. They are read from frameData at
	// execute time rather than captured here: DDGI ping-pongs its atlases every
	// frame, so an ID captured at build time goes stale. 0 means "not
	// available", which Execute() already handles.
	RGTextureHandle AddToGraph(
		RenderGraph& graph,
		Ref<Scene> scene,
		Ref<RGFrameData> frameData,
		const GBufferPass::GraphOutputs& gbuffer,
		uint32_t width,
		uint32_t height,
		RGTextureHandle shadowMask = {})
	{
		RDGTextureDesc outDesc;
		outDesc.width  = width;
		outDesc.height = height;
		outDesc.format = Format::RGBA8_UNORM;   // matches legacy m_OutputTex (GL_RGBA8)
		outDesc.usage  = TextureUsage::ColorAttachment | TextureUsage::Sampled;

		RGTextureHandle output = graph.CreateTexture(outDesc, "DeferredLighting.SceneColor");

		graph.AddPass(
			"DeferredLighting",
			[gbuffer, output, shadowMask](RenderGraphPassBuilder& builder)
			{
				// These reads are what order this pass after GBuffer.
				builder.ReadTexture(gbuffer.Position);
				builder.ReadTexture(gbuffer.Normal);
				builder.ReadTexture(gbuffer.Albedo);
				builder.ReadTexture(gbuffer.Specular);
				builder.ReadTexture(gbuffer.Depth);

				builder.SetColorAttachment(0, output, RGLoadOp::Clear,
					{ 0.0f, 0.0f, 0.0f, 1.0f });
			},
			[this, scene, frameData, gbuffer, width, height](
				RHICommandBuffer& cmd, RenderGraphResources& resources)
			{
				auto nativeID = [&resources](RGTextureHandle handle) -> unsigned int
				{
					RHITexture2D* texture = resources.GetTexture(handle);
					return texture ? static_cast<unsigned int>(texture->GetNativeID()) : 0u;
				};

				RenderResources bridge;
				bridge.GBufferPosition = nativeID(gbuffer.Position);
				bridge.GBufferNormal   = nativeID(gbuffer.Normal);
				bridge.GBufferAlbedo   = nativeID(gbuffer.Albedo);
				bridge.GBufferSpecular = nativeID(gbuffer.Specular);
				bridge.DepthTexture    = nativeID(gbuffer.Depth);
				bridge.VelocityTexture = nativeID(gbuffer.Velocity);

				// Produced by the migrated ShadowMapPass, published via frameData.
				bridge.ShadowMapDepth      = frameData ? frameData->ShadowMapDepthID : 0u;
				bridge.ShadowLightViewProj = frameData ? frameData->ShadowLightViewProj
				                                       : mat4(1.0f);

				// Still legacy-owned (compute + SSBO / image write, stage 7).
				// Read here rather than captured at build time: DDGI ping-pongs
				// its atlases each frame, so a build-time id goes stale.
				bridge.ShadowMask          = frameData ? frameData->ShadowMaskID : 0u;
				bridge.DDGIIrradianceAtlas = frameData ? frameData->DDGIIrradianceAtlasID : 0u;
				bridge.DDGIDepthAtlas      = frameData ? frameData->DDGIDepthAtlasID : 0u;

				// Cluster culling and the viewport both read m_Spec.
				m_Spec.Width  = width;
				m_Spec.Height = height;

				m_GraphManagedTarget = true;
				Execute(scene, bridge);
				m_GraphManagedTarget = false;
			});

		return output;
	}

	void Init(Ref<RHIFramebuffer> fb) override
	{
		m_Spec = { fb->GetWidth(), fb->GetHeight() };

		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f
		};
		unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };

		m_Shader = ShaderLibiray::Get("D:/Code/C++/Tsundere/res/shaders/DeferredLighting.shader");

		m_QuadVB = RHIBuffer::Create(BufferDesc{ (uint32_t)sizeof(quadVertices), BufferUsage::Vertex, false, quadVertices });
		m_QuadIB = RHIBuffer::Create(BufferDesc{ (uint32_t)(6 * sizeof(unsigned int)), BufferUsage::Index, false, quadIndices });
		m_QuadIndexCount = 6;
		VertexLayout quadLayout;
		quadLayout.stride = 4 * sizeof(float);
		quadLayout.attributes = {
			{ 0, VertexFormat::Float2, 0 },
			{ 1, VertexFormat::Float2, 2 * sizeof(float) },
		};
		PipelineDesc quadDesc;
		quadDesc.shader = m_Shader;
		quadDesc.vertexLayout = quadLayout;
		quadDesc.cullMode = CullMode::None;
		quadDesc.depthTest = false;
		m_QuadPipeline = RHIPipeline::Create(quadDesc);
		m_ClusterShader = RHIShader::CreateCompute("D:/Code/C++/Tsundere/res/shaders/ClusterLightCulling.shader");

		unsigned char white[4] = { 255, 255, 255, 255 };
		m_DefaultWhiteTex = RHITexture2D::Create(Texture2DDesc{
			1, 1, Format::RGBA8_UNORM, FilterMode::Linear, FilterMode::Linear,
			WrapMode::ClampToEdge, WrapMode::ClampToEdge, false, white });
		unsigned char black2[4] = { 0, 0, 0, 255 };
		m_DefaultBlackTex = RHITexture2D::Create(Texture2DDesc{
			1, 1, Format::RGBA8_UNORM, FilterMode::Linear, FilterMode::Linear,
			WrapMode::ClampToEdge, WrapMode::ClampToEdge, false, black2 });

		// 1x1 black fallback cubemap. No procedural-cubemap path in RHI yet, so
		// this one texture is still created through the GL backend directly.
		unsigned char black[4] = { 0, 0, 0, 255 };
		glGenTextures(1, &m_DefaultCubemap);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_DefaultCubemap);
		for (int face = 0; face < 6; face++)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		CreateOutputTex(m_Spec.Width, m_Spec.Height);
	}

	void Execute(Ref<Scene> scene, RenderResources& resources) override
	{
		if (!resources.GBufferPosition)
			return;

		vec3 lightDir = vec3(-0.5f, -1.0f, -0.5f);
		vec3 lightColor = vec3(1.0f);
		float ambientStrength = 0.1f;
		vec3 viewPos = currentcamera->getpos();
		for (auto entityID : scene->m_Registry.view<Component::DirectionalLight>())
		{
			auto& dl = scene->m_Registry.get<Component::DirectionalLight>(entityID);
			lightDir = dl.Direction;
			lightColor = dl.Color * dl.Intensity;
			ambientStrength = dl.Ambient;
			break;
		}

		GatherLocalLights(scene);
		EnsureLightBuffer();
		UploadLightBuffer();

		bool clusteredLighting = ClusteredLightingEnabled && !m_GPULights.empty();
		EnsureClusterBuffers();
		if (clusteredLighting)
			DispatchClusterCulling();

		// In graph mode the RenderGraph has already bound its own attachment and
		// set the viewport. Binding m_OutputFBO here would redirect the draw into
		// this pass's private target, leaving the graph's texture empty.
		auto cmd = RHIRenderer::GetCmd();
		if (!m_GraphManagedTarget)
		{
			// Fullscreen quad overwrites every pixel; no clear needed.
			RenderPassBeginInfo beginInfo;
			cmd->BeginRenderPass(m_OutputFBO, beginInfo);
		}
		cmd->SetDepthTest(false);

		// Build the per-pass UBO. Field order mirrors PerPass_DeferredLighting
		// in DeferredLighting.shader (std140 layout).
		DeferredLightingUBO ubo;
		mat4 viewNoTrans = mat4(mat3(currentcamera->GetViewFront()));
		mat4 proj = currentcamera->GetProj();
		ubo.u_InvViewProjNoTrans = inverse(proj * viewNoTrans);
		ubo.u_View              = currentcamera->GetViewFront();
		ubo.u_LightViewProj     = resources.ShadowLightViewProj;
		ubo.u_LightDir          = glm::vec4(lightDir, 0.0f);
		ubo.u_LightColor        = glm::vec4(lightColor, 0.0f);
		ubo.u_ViewPos           = glm::vec4(viewPos, 0.0f);
		ubo.u_ViewportSize      = glm::vec2((float)m_Spec.Width, (float)m_Spec.Height);
		ubo.u_ShadowMapSize     = glm::vec2(2048.0f, 2048.0f);
		ubo.u_AmbientStrength   = ambientStrength;
		ubo.u_Near              = 0.1f;
		ubo.u_Far               = 100.0f;
		ubo.u_LightSize         = 50.0f;
		ubo.u_DDGIEnabled       = (m_DDGIPass && DDGIEnabled) ? 1 : 0;
		ubo.u_ShadowMapEnabled  = resources.ShadowMapDepth ? 1 : 0;
		ubo.u_TileSize          = ClusterTileSize;
		ubo.u_ClusterCountX     = (int32_t)m_ClusterCountX;
		ubo.u_ClusterCountY     = (int32_t)m_ClusterCountY;
		ubo.u_ClusterCountZ     = (int32_t)m_ClusterCountZ;
		ubo.u_MaxLightsPerCluster = MaxLightsPerCluster;
		ubo.u_LocalLightCount   = (int32_t)m_GPULights.size();
		ubo.u_ClusteredLightingEnabled = clusteredLighting ? 1 : 0;
		ubo.u_DebugMode         = DebugMode;
		if (m_DDGIPass && DDGIEnabled)
		{
			const glm::ivec3& gs = m_DDGIPass->GetGridSize();
			ubo.u_DDGIGridSizeX = gs.x;
			ubo.u_DDGIGridSizeY = gs.y;
			ubo.u_DDGIGridSizeZ = gs.z;
			ubo.u_DDGISpacing     = m_DDGIPass->GetSpacing();
			ubo.u_DDGIProbeRadius = m_DDGIPass->GetProbeRadius();
			ubo.u_DDGIDepthSharpness = m_DDGIPass->GetDepthSharpness();
			ubo.u_DDGIProbesPerRow = m_DDGIPass->GetProbesPerRow();
			ubo.u_DDGIGridOrigin = glm::vec4(m_DDGIPass->GetGridOrigin(), 0.0f);
		}
		else
		{
			ubo.u_DDGIGridSizeX = 0;
			ubo.u_DDGIGridSizeY = 0;
			ubo.u_DDGIGridSizeZ = 0;
			ubo.u_DDGISpacing     = 0.0f;
			ubo.u_DDGIProbeRadius = 0.0f;
			ubo.u_DDGIDepthSharpness = 0.0f;
			ubo.u_DDGIProbesPerRow = 0;
			ubo.u_DDGIGridOrigin = glm::vec4(0.0f);
		}

		if (!m_DeferredLightingUBO)
			m_DeferredLightingUBO = RHIBuffer::Create(BufferDesc{ sizeof(ubo), BufferUsage::Uniform, true, nullptr });
		m_DeferredLightingUBO->Upload(&ubo, sizeof(ubo));

		// Bind samplers. GLSL samplers at binding 10..19 read from unit N.
		RHITexture2D* whiteTex = m_DefaultWhiteTex.get();
		RHITexture2D* blackTex = m_DefaultBlackTex.get();
		cmd->BindTexture2D(10, resources.GBufferPosition);
		cmd->BindTexture2D(11, resources.GBufferNormal);
		cmd->BindTexture2D(12, resources.GBufferAlbedo);
		cmd->BindTexture2D(13, resources.GBufferSpecular);
		cmd->BindTexture2D(14, resources.ShadowMask ? resources.ShadowMask : (whiteTex ? whiteTex->GetNativeID() : 0));
		cmd->BindTexture2D(15, resources.DepthTexture);
		if (currentcamera->skybox && currentcamera->skybox->m_Cmp)
			cmd->BindTextureCube(16, currentcamera->skybox->m_Cmp->GetNativeID());
		else
			cmd->BindTextureCube(16, m_DefaultCubemap);
		if (ubo.u_DDGIEnabled == 1 && resources.DDGIIrradianceAtlas)
			cmd->BindTexture2D(17, resources.DDGIIrradianceAtlas);
		else
			cmd->BindTexture2D(17, blackTex ? blackTex->GetNativeID() : 0);
		if (ubo.u_DDGIEnabled == 1 && resources.DDGIDepthAtlas)
			cmd->BindTexture2D(18, resources.DDGIDepthAtlas);
		else
			cmd->BindTexture2D(18, whiteTex ? whiteTex->GetNativeID() : 0);
		cmd->BindTexture2D(19, resources.ShadowMapDepth
			? resources.ShadowMapDepth
			: (whiteTex ? whiteTex->GetNativeID() : 0));

		// Descriptor set: UBO at binding 0 + 3 SSBOs.
		m_DeferredDescriptorSet->Reset();
		m_DeferredDescriptorSet->BindUniformBuffer(0, m_DeferredLightingUBO);
		if (m_LightSSBO)        m_DeferredDescriptorSet->BindStorageBuffer(LightBufferBinding,  m_LightSSBO);
		if (m_ClusterMetaSSBO)  m_DeferredDescriptorSet->BindStorageBuffer(ClusterMetaBinding,  m_ClusterMetaSSBO);
		if (m_ClusterIndexSSBO) m_DeferredDescriptorSet->BindStorageBuffer(ClusterIndexBinding, m_ClusterIndexSSBO);
		m_DeferredDescriptorSet->Apply(0);

		m_Shader->Bind();

		cmd->BindPipeline(m_QuadPipeline);
		cmd->BindVertexBuffer(m_QuadVB);
		cmd->BindIndexBuffer(m_QuadIB);
		cmd->DrawIndexed(m_QuadIndexCount);

			// --- DDGI probe debug overlay (rendered into the lighting output) ---
			if (m_DDGIPass && m_DDGIPass->ShowProbes)
			{
				mat4 view = currentcamera->GetViewFront();
				mat4 proj = currentcamera->GetProj();
				m_DDGIPass->RenderProbeDebug(view, proj);
			}

		cmd->SetDepthTest(true);

		// In graph mode the graph owns the render target: it unbinds via
		// EndRenderPass, and the output handle is returned by AddToGraph rather
		// than published through RenderResources.
		if (!m_GraphManagedTarget)
		{
			cmd->EndRenderPass();
			resources.SceneColorTexture = (unsigned int)m_OutputFBO->GetColorAttachmentID(0);
		}
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
		CreateOutputTex(w, h);
	}

private:
	struct GPULight
	{
		glm::vec4 PositionRadius = glm::vec4(0.0f);
		glm::vec4 DirectionType = glm::vec4(0.0f);
		glm::vec4 ColorIntensity = glm::vec4(0.0f);
		glm::vec4 Params = glm::vec4(0.0f);
	};

	struct GPUClusterMeta
	{
		unsigned int Offset = 0;
		unsigned int Count = 0;
		unsigned int Pad0 = 0;
		unsigned int Pad1 = 0;
	};

	static constexpr unsigned int LightBufferBinding = 10;
	static constexpr unsigned int ClusterMetaBinding = 11;
	static constexpr unsigned int ClusterIndexBinding = 12;

	// PerPass_DeferredLighting UBO. Field order and padding exactly match
	// DeferredLighting.shader's std140 block. Total size 368 bytes.
	struct DeferredLightingUBO
	{
		glm::mat4 u_InvViewProjNoTrans;     //   0
		glm::mat4 u_View;                   //  64
		glm::mat4 u_LightViewProj;          // 128
		glm::vec4 u_LightDir;               // 192 (vec3→vec4)
		glm::vec4 u_LightColor;             // 208
		glm::vec4 u_ViewPos;                // 224
		glm::vec2 u_ViewportSize;           // 240
		glm::vec2 u_ShadowMapSize;          // 248
		float     u_AmbientStrength;        // 256
		float     u_Near;                   // 260
		float     u_Far;                    // 264
		float     u_LightSize;              // 268
		float     u_DDGISpacing;            // 272
		float     u_DDGIProbeRadius;        // 276
		float     u_DDGIDepthSharpness;     // 280
		int32_t   u_DDGIEnabled;            // 284
		int32_t   u_DDGIProbesPerRow;       // 288
		int32_t   u_TileSize;               // 292
		int32_t   u_ClusterCountX;          // 296
		int32_t   u_ClusterCountY;          // 300
		int32_t   u_ClusterCountZ;          // 304
		int32_t   u_MaxLightsPerCluster;    // 308
		int32_t   u_LocalLightCount;        // 312
		int32_t   u_ClusteredLightingEnabled;// 316
		int32_t   u_DebugMode;              // 320
		int32_t   u_ShadowMapEnabled;       // 324
		int32_t   u_DDGIGridSizeX;          // 328
		int32_t   u_DDGIGridSizeY;          // 332
		int32_t   u_DDGIGridSizeZ;          // 336
		int32_t   _pad0[3];                 // 340..351 — std140 pad: vec4 needs 16-byte align
		glm::vec4 u_DDGIGridOrigin;         // 352
	};
	static_assert(sizeof(DeferredLightingUBO) == 368,
		"DeferredLightingUBO must be 368 bytes (one UBO field offset is wrong)");

	void CreateOutputTex(unsigned int w, unsigned int h)
	{
		m_OutputFBO = RHIFramebuffer::Create(FramebufferDesc{
			w, h, { { Format::RGBA8_UNORM, 1 } }, false, 1 });
	}

	void GatherLocalLights(Ref<Scene> scene)
	{
		m_GPULights.clear();
		if (!scene)
			return;

		for (auto [entityID, transform, light] : scene->m_Registry.view<Component::Transform, Component::PointLight>().each())
		{
			if ((int)m_GPULights.size() >= MaxLocalLights)
				break;
			if (light.Radius <= 0.0f || light.Intensity <= 0.0f)
				continue;

			GPULight gpuLight;
			gpuLight.PositionRadius = glm::vec4(transform.Position, light.Radius);
			gpuLight.DirectionType = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			gpuLight.ColorIntensity = glm::vec4(light.Color, light.Intensity);
			gpuLight.Params = glm::vec4(light.Falloff, 0.0f, 0.0f, 0.0f);
			m_GPULights.push_back(gpuLight);
		}
	}

	void EnsureLightBuffer()
	{
		size_t requiredSize = std::max<size_t>(sizeof(GPULight), m_GPULights.size() * sizeof(GPULight));
		if (!m_LightSSBO || m_LightSSBO->GetSize() < requiredSize)
			m_LightSSBO = RHIBuffer::Create(BufferDesc{ (uint32_t)requiredSize, BufferUsage::Storage, false, nullptr });
	}

	void UploadLightBuffer()
	{
		if (!m_LightSSBO)
			return;

		if (!m_GPULights.empty())
			m_LightSSBO->Upload(m_GPULights.data(), (uint32_t)(m_GPULights.size() * sizeof(GPULight)));
		else
		{
			GPULight dummy{};
			m_LightSSBO->Upload(&dummy, (uint32_t)sizeof(GPULight));
		}
		m_LightSSBO->BindToSlot(LightBufferBinding);
	}

	void EnsureClusterBuffers()
	{
		ClusterTileSize = std::clamp(ClusterTileSize, 8, 128);
		ClusterZSlices = std::clamp(ClusterZSlices, 1, 128);
		MaxLightsPerCluster = std::clamp(MaxLightsPerCluster, 1, 256);

		m_ClusterCountX = (m_Spec.Width + ClusterTileSize - 1) / ClusterTileSize;
		m_ClusterCountY = (m_Spec.Height + ClusterTileSize - 1) / ClusterTileSize;
		m_ClusterCountZ = (unsigned int)ClusterZSlices;

		size_t clusterCount = (size_t)m_ClusterCountX * (size_t)m_ClusterCountY * (size_t)m_ClusterCountZ;
		size_t metaSize = std::max<size_t>(sizeof(GPUClusterMeta), clusterCount * sizeof(GPUClusterMeta));
		size_t indexSize = std::max<size_t>(sizeof(unsigned int), clusterCount * (size_t)MaxLightsPerCluster * sizeof(unsigned int));

		if (!m_ClusterMetaSSBO || m_ClusterMetaSSBO->GetSize() < metaSize)
			m_ClusterMetaSSBO = RHIBuffer::Create(BufferDesc{ (uint32_t)metaSize, BufferUsage::Storage, false, nullptr });
		if (!m_ClusterIndexSSBO || m_ClusterIndexSSBO->GetSize() < indexSize)
			m_ClusterIndexSSBO = RHIBuffer::Create(BufferDesc{ (uint32_t)indexSize, BufferUsage::Storage, false, nullptr });

		m_ClusterMetaSSBO->BindToSlot(ClusterMetaBinding);
		m_ClusterIndexSSBO->BindToSlot(ClusterIndexBinding);
	}

	void DispatchClusterCulling()
	{
		if (!m_ClusterShader || !m_LightSSBO || !m_ClusterMetaSSBO || !m_ClusterIndexSSBO)
			return;

		// PerPass_ClusterCull UBO (std140 layout — see ClusterLightCulling.shader).
		// Field order and padding must match the GLSL block exactly.
		struct ClusterCullUBO
		{
			glm::mat4 u_View;             // offset   0
			glm::mat4 u_InvProj;          // offset  64
			glm::vec2 u_ViewportSize;     // offset 128
			int32_t   u_TileSize;         // offset 136
			int32_t   u_ClusterCountX;    // offset 140
			int32_t   u_ClusterCountY;    // offset 144
			int32_t   u_ClusterCountZ;    // offset 148
			int32_t   u_MaxLightsPerCluster; // offset 152
			int32_t   u_LocalLightCount;  // offset 156
			float     u_Near;             // offset 160
			float     u_Far;              // offset 164
		};
		static_assert(sizeof(ClusterCullUBO) == 168, "ClusterCullUBO must match std140 PerPass_ClusterCull");

		ClusterCullUBO ubo;
		ubo.u_View    = currentcamera->GetViewFront();
		ubo.u_InvProj = glm::inverse(currentcamera->GetProj());
		ubo.u_ViewportSize = glm::vec2((float)m_Spec.Width, (float)m_Spec.Height);
		ubo.u_TileSize = ClusterTileSize;
		ubo.u_ClusterCountX = (int32_t)m_ClusterCountX;
		ubo.u_ClusterCountY = (int32_t)m_ClusterCountY;
		ubo.u_ClusterCountZ = (int32_t)m_ClusterCountZ;
		ubo.u_MaxLightsPerCluster = MaxLightsPerCluster;
		ubo.u_LocalLightCount = (int32_t)m_GPULights.size();
		ubo.u_Near = 0.1f;
		ubo.u_Far  = 100.0f;

		if (!m_ClusterCullUBO)
			m_ClusterCullUBO = RHIBuffer::Create(BufferDesc{ sizeof(ubo), BufferUsage::Uniform, true, nullptr });
		m_ClusterCullUBO->Upload(&ubo, sizeof(ubo));

		// Bind shader, descriptor set (UBO + SSBOs), then dispatch.
		m_ClusterShader->Bind();
		m_ClusterDescriptorSet->Reset();
		m_ClusterDescriptorSet->BindUniformBuffer(0, m_ClusterCullUBO);
		BindLightBuffersVia(m_ClusterDescriptorSet);
		m_ClusterDescriptorSet->Apply(0);

		m_ClusterShader->DispatchCompute(m_ClusterCountX, m_ClusterCountY, m_ClusterCountZ);
		RHIRenderer::GetCmd()->ResourceBarrier(BarrierFlags::StorageBuffer);
	}

	void BindLightBuffersVia(Ref<RHIDescriptorSet> set)
	{
		if (m_LightSSBO)        set->BindStorageBuffer(LightBufferBinding,    m_LightSSBO);
		if (m_ClusterMetaSSBO)  set->BindStorageBuffer(ClusterMetaBinding,    m_ClusterMetaSSBO);
		if (m_ClusterIndexSSBO) set->BindStorageBuffer(ClusterIndexBinding,   m_ClusterIndexSSBO);
	}

	void BindLightBuffers()
	{
		if (m_LightSSBO)
			m_LightSSBO->BindToSlot(LightBufferBinding);
		if (m_ClusterMetaSSBO)
			m_ClusterMetaSSBO->BindToSlot(ClusterMetaBinding);
		if (m_ClusterIndexSSBO)
			m_ClusterIndexSSBO->BindToSlot(ClusterIndexBinding);
	}

	FrameBufferSpecification m_Spec;
	Ref<RHIShader> m_Shader;
	Ref<RHIShader> m_ClusterShader;
	Ref<RHIBuffer> m_LightSSBO;
	Ref<RHIBuffer> m_ClusterMetaSSBO;
	Ref<RHIBuffer> m_ClusterIndexSSBO;
	std::vector<GPULight> m_GPULights;
	unsigned int m_ClusterCountX = 1;
	unsigned int m_ClusterCountY = 1;
	unsigned int m_ClusterCountZ = 1;
	Ref<RHIBuffer> m_QuadVB;
	Ref<RHIBuffer> m_QuadIB;
	Ref<RHIPipeline> m_QuadPipeline;
	unsigned int m_QuadIndexCount = 0;
	Ref<RHIFramebuffer> m_OutputFBO;

	// Per-cluster-cull UBO (descriptor set 0, binding 0).
	Ref<RHIBuffer> m_ClusterCullUBO;
	Ref<RHIDescriptorSet> m_ClusterDescriptorSet = RHIDescriptorSet::Create();

	// Per-deferred-lighting UBO and descriptor set.
	Ref<RHIBuffer> m_DeferredLightingUBO;
	Ref<RHIDescriptorSet> m_DeferredDescriptorSet = RHIDescriptorSet::Create();

	// True only while executing inside a RenderGraph pass. The graph has already
	// bound its own attachment and set the viewport, so Execute() must not bind
	// m_OutputFBO over it. See AddToGraph().
	bool m_GraphManagedTarget = false;
	Ref<RHITexture2D> m_DefaultWhiteTex;
	Ref<RHITexture2D> m_DefaultBlackTex;
	unsigned int m_DefaultCubemap = 0;  // raw: no procedural-cubemap path in RHI yet
	DDGIPass* m_DDGIPass = nullptr;
};
