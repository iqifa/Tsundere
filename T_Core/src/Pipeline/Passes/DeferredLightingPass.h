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

	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();

		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f
		};
		unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };

		m_QuadVA = CreatePtr<VertexArray>(4);
		m_QuadVB = CreatePtr<VertexBuffer>(quadVertices, sizeof(quadVertices));
		m_QuadIB = CreatePtr<IndexBuffer>(quadIndices, 6);
		VertexBufferLayout quadLayout;
		quadLayout.Push<float>(2);
		quadLayout.Push<float>(2);
		m_QuadVA->AddBuffer(*m_QuadVB, quadLayout);

		m_Shader = ShaderLibiray::Get("D:/Code/C++/Tsundere/res/shaders/DeferredLighting.shader");
		m_ClusterShader = GLShader::CreateCompute("D:/Code/C++/Tsundere/res/shaders/ClusterLightCulling.shader");

		unsigned char white[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &m_DefaultWhiteTex);
		glBindTexture(GL_TEXTURE_2D, m_DefaultWhiteTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// Default black texture for DDGI fallback
		unsigned char black2[4] = { 0, 0, 0, 255 };
		glGenTextures(1, &m_DefaultBlackTex);
		glBindTexture(GL_TEXTURE_2D, m_DefaultBlackTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black2);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

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
		if (!m_GraphManagedTarget)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
			glViewport(0, 0, m_Spec.Width, m_Spec.Height);
		}
		glDisable(GL_DEPTH_TEST);

		m_Shader->Bind();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, resources.GBufferPosition);
		m_Shader->SetUniform1i("u_GBufferPosition", 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, resources.GBufferNormal);
		m_Shader->SetUniform1i("u_GBufferNormal", 1);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, resources.GBufferAlbedo);
		m_Shader->SetUniform1i("u_GBufferAlbedo", 2);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, resources.GBufferSpecular);
		m_Shader->SetUniform1i("u_GBufferSpecular", 3);

		glActiveTexture(GL_TEXTURE4);
		if (resources.ShadowMask)
			glBindTexture(GL_TEXTURE_2D, resources.ShadowMask);
		else
			glBindTexture(GL_TEXTURE_2D, m_DefaultWhiteTex);
		m_Shader->SetUniform1i("u_ShadowMask", 4);

		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, resources.DepthTexture);
		m_Shader->SetUniform1i("u_Depth", 5);

		m_Shader->SetUniformVec3("u_LightDir", lightDir);
		m_Shader->SetUniformVec3("u_LightColor", lightColor);
		m_Shader->SetUniform1f("u_AmbientStrength", ambientStrength);
		m_Shader->SetUniformVec3("u_ViewPos", viewPos);

		mat4 viewNoTrans = mat4(mat3(currentcamera->GetViewFront()));
		mat4 proj = currentcamera->GetProj();
		mat4 invViewProjNoTrans = inverse(proj * viewNoTrans);
		m_Shader->SetUniformMat4f("u_InvViewProjNoTrans", invViewProjNoTrans);

		glActiveTexture(GL_TEXTURE6);
		if (currentcamera->skybox && currentcamera->skybox->m_Cmp)
			glBindTexture(GL_TEXTURE_CUBE_MAP, currentcamera->skybox->m_Cmp->GetMap());
		else
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_DefaultCubemap);
		m_Shader->SetUniform1i("u_Skybox", 6);

		// --- DDGI indirect diffuse ---
		glActiveTexture(GL_TEXTURE7);
		if (m_DDGIPass && DDGIEnabled && resources.DDGIIrradianceAtlas)
		{
			glBindTexture(GL_TEXTURE_2D, resources.DDGIIrradianceAtlas);
			m_Shader->SetUniform1i("u_DDGIEnabled", 1);
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, m_DefaultBlackTex);
			m_Shader->SetUniform1i("u_DDGIEnabled", 0);
		}
		m_Shader->SetUniform1i("u_DDGIIrradiance", 7);

		glActiveTexture(GL_TEXTURE8);
		if (m_DDGIPass && DDGIEnabled && resources.DDGIDepthAtlas)
			glBindTexture(GL_TEXTURE_2D, resources.DDGIDepthAtlas);
		else
			glBindTexture(GL_TEXTURE_2D, m_DefaultWhiteTex);
		m_Shader->SetUniform1i("u_DDGIDepth", 8);

		// --- Shadow Map (texture unit 9) ---
		glActiveTexture(GL_TEXTURE9);
		if (resources.ShadowMapDepth)
		{
			glBindTexture(GL_TEXTURE_2D, resources.ShadowMapDepth);
			m_Shader->SetUniform1i("u_ShadowMapEnabled", 1);
			m_Shader->SetUniformMat4f("u_LightViewProj", resources.ShadowLightViewProj);
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, m_DefaultWhiteTex);
			m_Shader->SetUniform1i("u_ShadowMapEnabled", 0);
			m_Shader->SetUniformMat4f("u_LightViewProj", glm::mat4(1.0f));
		}
		m_Shader->SetUniform1i("u_ShadowMap", 9);
		m_Shader->SetUniformVec2("u_ShadowMapSize", glm::vec2(2048.0f, 2048.0f));
		m_Shader->SetUniform1f("u_LightSize", 50.0f);

		// DDGI grid parameters
		if (m_DDGIPass && DDGIEnabled)
		{
			const glm::ivec3& gs = m_DDGIPass->GetGridSize();
			m_Shader->SetUniform1i("u_DDGIGridSizeX", gs.x);
			m_Shader->SetUniform1i("u_DDGIGridSizeY", gs.y);
			m_Shader->SetUniform1i("u_DDGIGridSizeZ", gs.z);
			m_Shader->SetUniformVec3("u_DDGIGridOrigin", m_DDGIPass->GetGridOrigin());
			m_Shader->SetUniform1f("u_DDGISpacing", m_DDGIPass->GetSpacing());
			m_Shader->SetUniform1f("u_DDGIProbeRadius", m_DDGIPass->GetProbeRadius());
			m_Shader->SetUniform1f("u_DDGIDepthSharpness", m_DDGIPass->GetDepthSharpness());
			m_Shader->SetUniform1i("u_DDGIProbesPerRow", m_DDGIPass->GetProbesPerRow());
		}
		else
		{
			m_Shader->SetUniform1i("u_DDGIGridSizeX", 0);
		}

		m_Shader->SetUniform1f("u_Near", 0.1f);
		m_Shader->SetUniform1f("u_Far", 100.0f);
		m_Shader->SetUniform1i("u_DebugMode", DebugMode);
		m_Shader->SetUniform1i("u_LocalLightCount", (int)m_GPULights.size());
		m_Shader->SetUniform1i("u_ClusteredLightingEnabled", clusteredLighting ? 1 : 0);
		m_Shader->SetUniformVec2("u_ViewportSize", glm::vec2((float)m_Spec.Width, (float)m_Spec.Height));
		m_Shader->SetUniform1i("u_TileSize", ClusterTileSize);
		m_Shader->SetUniform1i("u_ClusterCountX", (int)m_ClusterCountX);
		m_Shader->SetUniform1i("u_ClusterCountY", (int)m_ClusterCountY);
		m_Shader->SetUniform1i("u_ClusterCountZ", (int)m_ClusterCountZ);
		m_Shader->SetUniform1i("u_MaxLightsPerCluster", MaxLightsPerCluster);
		m_Shader->SetUniformMat4f("u_View", currentcamera->GetViewFront());
		BindLightBuffers();

		Renderer renderer;
		renderer.DrawElement(*m_QuadVA, *m_QuadIB, *m_Shader);

			// --- DDGI probe debug overlay (rendered into the lighting output) ---
			if (m_DDGIPass && m_DDGIPass->ShowProbes)
			{
				mat4 view = currentcamera->GetViewFront();
				mat4 proj = currentcamera->GetProj();
				m_DDGIPass->RenderProbeDebug(view, proj);
			}

		glEnable(GL_DEPTH_TEST);

		// In graph mode the graph owns the render target: it unbinds via
		// EndRenderPass, and the output handle is returned by AddToGraph rather
		// than published through RenderResources.
		if (!m_GraphManagedTarget)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			resources.SceneColorTexture = m_OutputTex;
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

	void CreateOutputTex(unsigned int w, unsigned int h)
	{
		if (m_OutputTex)
			glDeleteTextures(1, &m_OutputTex);
		if (m_OutputFBO)
			glDeleteFramebuffers(1, &m_OutputFBO);

		glGenTextures(1, &m_OutputTex);
		glBindTexture(GL_TEXTURE_2D, m_OutputTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenFramebuffers(1, &m_OutputFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTex, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
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
			m_LightSSBO = StorageBuffer::Create(requiredSize, nullptr, LightBufferBinding);
	}

	void UploadLightBuffer()
	{
		if (!m_LightSSBO)
			return;

		if (!m_GPULights.empty())
			m_LightSSBO->SetData(m_GPULights.data(), m_GPULights.size() * sizeof(GPULight));
		else
		{
			GPULight dummy{};
			m_LightSSBO->SetData(&dummy, sizeof(GPULight));
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
			m_ClusterMetaSSBO = StorageBuffer::Create(metaSize, nullptr, ClusterMetaBinding);
		if (!m_ClusterIndexSSBO || m_ClusterIndexSSBO->GetSize() < indexSize)
			m_ClusterIndexSSBO = StorageBuffer::Create(indexSize, nullptr, ClusterIndexBinding);

		m_ClusterMetaSSBO->BindToSlot(ClusterMetaBinding);
		m_ClusterIndexSSBO->BindToSlot(ClusterIndexBinding);
	}

	void DispatchClusterCulling()
	{
		if (!m_ClusterShader || !m_LightSSBO || !m_ClusterMetaSSBO || !m_ClusterIndexSSBO)
			return;

		m_ClusterShader->Bind();
		m_ClusterShader->SetUniformMat4f("u_View", currentcamera->GetViewFront());
		m_ClusterShader->SetUniformMat4f("u_InvProj", glm::inverse(currentcamera->GetProj()));
		m_ClusterShader->SetUniformVec2("u_ViewportSize", glm::vec2((float)m_Spec.Width, (float)m_Spec.Height));
		m_ClusterShader->SetUniform1i("u_TileSize", ClusterTileSize);
		m_ClusterShader->SetUniform1i("u_ClusterCountX", (int)m_ClusterCountX);
		m_ClusterShader->SetUniform1i("u_ClusterCountY", (int)m_ClusterCountY);
		m_ClusterShader->SetUniform1i("u_ClusterCountZ", (int)m_ClusterCountZ);
		m_ClusterShader->SetUniform1i("u_MaxLightsPerCluster", MaxLightsPerCluster);
		m_ClusterShader->SetUniform1i("u_LocalLightCount", (int)m_GPULights.size());
		m_ClusterShader->SetUniform1f("u_Near", 0.1f);
		m_ClusterShader->SetUniform1f("u_Far", 100.0f);

		BindLightBuffers();
		m_ClusterShader->DispatchCompute(m_ClusterCountX, m_ClusterCountY, m_ClusterCountZ);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
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
	Ref<StorageBuffer> m_LightSSBO;
	Ref<StorageBuffer> m_ClusterMetaSSBO;
	Ref<StorageBuffer> m_ClusterIndexSSBO;
	std::vector<GPULight> m_GPULights;
	unsigned int m_ClusterCountX = 1;
	unsigned int m_ClusterCountY = 1;
	unsigned int m_ClusterCountZ = 1;
	Ptr<VertexArray> m_QuadVA;
	Ptr<VertexBuffer> m_QuadVB;
	Ptr<IndexBuffer> m_QuadIB;
	unsigned int m_OutputTex = 0;
	unsigned int m_OutputFBO = 0;

	// True only while executing inside a RenderGraph pass. The graph has already
	// bound its own attachment and set the viewport, so Execute() must not bind
	// m_OutputFBO over it. See AddToGraph().
	bool m_GraphManagedTarget = false;
	unsigned int m_DefaultWhiteTex = 0;
	unsigned int m_DefaultBlackTex = 0;
	unsigned int m_DefaultCubemap = 0;
	DDGIPass* m_DDGIPass = nullptr;
};
