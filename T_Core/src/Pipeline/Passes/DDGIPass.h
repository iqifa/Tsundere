#pragma once
#include "Pipeline/Passes/PassCommon.h"

// ============================================================================
// DDGIPass — Dynamic Diffuse Global Illumination probe update
// ============================================================================
// Casts rays from a 3D grid of probes into the scene using the BVH.
// Accumulates irradiance into an octahedral-map texture atlas with
// temporal blending (ping-pong history). Probes are updated round-robin.
class  DDGIPass : public RenderPass
{
public:
	bool Enabled = true;

	// Volume configuration
	glm::ivec3 GridSize = { 8, 4, 8 };
	float     Spacing = 2.0f;
	float     ProbeRadius = 3.0f;
	int       RaysPerProbe = 256;
	int       ProbesPerUpdate = 128;
	float     Hysteresis = 0.97f;
	float     DepthSharpness = 50.0f;
	bool      ScrollWithCamera = true;
	glm::vec3 GridOrigin = glm::vec3(0.0f);
	bool      AutoPlaceGrid = true;


	Ref<RHIBuffer>m_MaterialSSBO;

	void Init(Ref<RHIFramebuffer> fb) override
	{
		m_Spec = { fb->GetWidth(), fb->GetHeight() };
		m_UpdateShader = RHIShader::CreateCompute("D:/Code/C++/Tsundere/res/shaders/DDGIProbeUpdate.shader");
		if (!m_UpdateShader || m_UpdateShader->GetID() == 0)
		{
			Error_Core("DDGIPass: Failed to create DDGIProbeUpdate compute shader!");
			Enabled = false;
		}
		RebuildAtlases();
	}

	void Execute(Ref<Scene> scene, RenderResources& resources) override
	{
		if (!Enabled || !m_BVHBuilder)
			return;

		// Rebuild atlas/SSBO if grid size changed
		if (GridSize != m_LastGridSize)
		{
			RebuildAtlases();
			m_LastGridSize = GridSize;
		}

		// Rebuild BVH only when scene geometry changed
		if (m_BVHBuilder->IsDirty())
		{
			m_BVHBuilder->GatherTriangles(scene);
			m_BVHBuilder->BuildBVH(4);
			m_BVHBuilder->UploadToGPU();
			m_BVHBuilder->MarkClean();
		}

		if (!m_BVHBuilder->GetTriangleBuffer() || !m_BVHBuilder->GetBVHNodeBuffer())
			return;

		// Update camera scroll
		if (ScrollWithCamera && AutoPlaceGrid && currentcamera)
			UpdateScrollOffset();

		// Upload current probe positions
		UpdateProbeRayDataSSBO();

		// PerPass_DDGI UBO (std140, 32 bytes). Struct declared in class scope.

		DDGIUBO ubo;
		ubo.u_TotalProbes     = m_TotalProbes;
		ubo.u_ProbesPerRow    = m_ProbesPerRow;
		ubo.u_RaysPerProbe    = RaysPerProbe;
		ubo.u_ProbesPerUpdate = ProbesPerUpdate;
		ubo.u_ProbeOffset     = m_CurrentProbeOffset;
		ubo.u_Hysteresis      = Hysteresis;
		ubo.u_FrameSeed       = (float)m_FrameIdx;
		if (!m_DDGIUBO)
			m_DDGIUBO = RHIBuffer::Create(BufferDesc{ sizeof(ubo), BufferUsage::Uniform, true, nullptr });
		m_DDGIUBO->Upload(&ubo, sizeof(ubo));

		// Bind output atlases as images (write-only)
		m_IrradianceAtlas->BindAsImage(1, ImageAccess::WriteOnly);
		m_DepthAtlas->BindAsImage(2, ImageAccess::WriteOnly);

		// Bind history atlases (read-only, for temporal blend)
		if (m_FrameIdx > 0)
		{
			m_IrradianceAtlasPrev->BindAsImage(3, ImageAccess::ReadOnly);
			m_DepthAtlasPrev->BindAsImage(4, ImageAccess::ReadOnly);
		}
		else
		{
			m_IrradianceAtlas->BindAsImage(3, ImageAccess::ReadOnly);
			m_DepthAtlas->BindAsImage(4, ImageAccess::ReadOnly);
		}

		// Build and bind material SSBO (slot 7)
		BuildMaterialSSBO(scene);

		// Bind skybox cubemap
		auto cmd = RHIRenderer::GetCmd();
		if (currentcamera && currentcamera->skybox && currentcamera->skybox->m_Cmp)
			cmd->BindTextureCube(8, currentcamera->skybox->m_Cmp->GetNativeID());

		// Descriptor set: UBO at 0 + 4 SSBOs (probe data, BVH tris, BVH nodes, materials).
		m_DDGIDescriptorSet->Reset();
		m_DDGIDescriptorSet->BindUniformBuffer(0, m_DDGIUBO);
		if (m_ProbeRayDataSSBO) m_DDGIDescriptorSet->BindStorageBuffer(0, m_ProbeRayDataSSBO);
		if (m_BVHBuilder)
		{
			if (auto t = m_BVHBuilder->GetTriangleBuffer()) m_DDGIDescriptorSet->BindStorageBuffer(5, t);
			if (auto b = m_BVHBuilder->GetBVHNodeBuffer())  m_DDGIDescriptorSet->BindStorageBuffer(6, b);
		}
		if (m_MaterialSSBO) m_DDGIDescriptorSet->BindStorageBuffer(7, m_MaterialSSBO);
		m_DDGIDescriptorSet->Apply(0);

		m_UpdateShader->Bind();

		// Dispatch: ProbesPerUpdate * RaysPerProbe threads
		int totalRays = ProbesPerUpdate * RaysPerProbe;
		int groups = (totalRays + 63) / 64;
		m_UpdateShader->DispatchCompute(groups);

		cmd->ResourceBarrier(BarrierFlags::ShaderImage | BarrierFlags::TextureFetch);

		// Unbind images so they can be sampled as textures later
		m_IrradianceAtlas->UnbindAsImage(1);
		m_DepthAtlas->UnbindAsImage(2);
		m_IrradianceAtlas->UnbindAsImage(3);
		m_DepthAtlas->UnbindAsImage(4);

		// Advance round-robin
		m_CurrentProbeOffset = (m_CurrentProbeOffset + ProbesPerUpdate) % m_TotalProbes;
			// Log to confirm DDGI is active
			if (m_FrameIdx % 60 == 0)
						Info_Core("DDGI: frame {}, updating {}/{} probes, atlas {}x{}", m_FrameIdx, ProbesPerUpdate, m_TotalProbes, m_IrradianceAtlasPrev->GetWidth(), m_IrradianceAtlasPrev->GetHeight());
		m_FrameIdx++;

		// Ping-pong: swap current and history atlases
		std::swap(m_IrradianceAtlas, m_IrradianceAtlasPrev);
		std::swap(m_DepthAtlas, m_DepthAtlasPrev);

		// Store outputs in RenderResources
		resources.DDGIIrradianceAtlas = (unsigned int)m_IrradianceAtlasPrev->GetNativeID();
		resources.DDGIDepthAtlas = (unsigned int)m_DepthAtlasPrev->GetNativeID();
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
	}

	void SetBVHBuilder(Ref<BVHBuilder> builder)
	{
		m_BVHBuilder = builder;
	}

		// Reset all accumulated irradiance (clears atlas, restarts round-robin)
		// Does NOT change probe positions or grid origin.
		void Reset()
		{
			// Recreate atlas textures (clears to black)
			int irradianceAtlasW = m_ProbesPerRow * 8;
			int irradianceAtlasH = ((m_TotalProbes + m_ProbesPerRow - 1) / m_ProbesPerRow) * 8;
			int depthAtlasW = m_ProbesPerRow * 16;
			int depthAtlasH = ((m_TotalProbes + m_ProbesPerRow - 1) / m_ProbesPerRow) * 16;

			m_IrradianceAtlas     = RHIStorageImage::Create({ (uint32_t)irradianceAtlasW, (uint32_t)irradianceAtlasH, Format::RGBA16F });
			m_IrradianceAtlasPrev = RHIStorageImage::Create({ (uint32_t)irradianceAtlasW, (uint32_t)irradianceAtlasH, Format::RGBA16F });
			m_DepthAtlas          = RHIStorageImage::Create({ (uint32_t)depthAtlasW, (uint32_t)depthAtlasH, Format::R16F });
			m_DepthAtlasPrev      = RHIStorageImage::Create({ (uint32_t)depthAtlasW, (uint32_t)depthAtlasH, Format::R16F });

			m_ScrollOffset        = glm::ivec3(0);
			m_CurrentProbeOffset  = 0;
			m_FrameIdx            = 0;
		}


	// Accessors for DeferredLightingPass
	unsigned int GetIrradianceAtlasID() const { return m_IrradianceAtlasPrev ? (unsigned int)m_IrradianceAtlasPrev->GetNativeID() : 0; }
	unsigned int GetDepthAtlasID() const { return m_DepthAtlasPrev ? (unsigned int)m_DepthAtlasPrev->GetNativeID() : 0; }
	int GetProbesPerRow() const { return m_ProbesPerRow; }
	const glm::ivec3& GetGridSize() const { return GridSize; }
	float GetSpacing() const { return Spacing; }
	float GetProbeRadius() const { return ProbeRadius; }
	float GetDepthSharpness() const { return DepthSharpness; }
	glm::vec3 GetGridOrigin() const { return GridOrigin + glm::vec3(m_ScrollOffset) * Spacing; }
	int GetTotalProbes() const { return m_TotalProbes; }

		// Probe debug visualization
		bool ShowProbes = false;
		void RenderProbeDebug(mat4 view, mat4 proj)
		{
			if (!ShowProbes || m_TotalProbes == 0) return;
			if (!m_ProbeVisShader)
				m_ProbeVisShader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/ProbeVis.shader");
			if (!m_ProbeVisPipeline)
				BuildProbeVisGeometry();

			mat4 vp = proj * view;
			auto cmd = RHIRenderer::GetCmd();
			m_ProbeVisShader->Bind();
			cmd->SetDepthTest(false);
			cmd->SetPointSize(10.0f);

			// Draw probe points
			m_ProbeVisShader->SetUniformVec3("u_Color", glm::vec3(0.0f, 1.0f, 0.5f));
			cmd->BindPipeline(m_ProbeVisPipeline);
			cmd->BindVertexBuffer(m_ProbeVisVB);
			for (int i = 0; i < m_TotalProbes; i++)
			{
				glm::vec3 pos = DDGI::ProbeWorldPos(GridSize, GridOrigin, Spacing, m_ScrollOffset, i);
				mat4 model = glm::translate(glm::mat4(1.0f), pos);
				m_ProbeVisShader->SetUniformMat4f("u_MVP", vp * model);
				cmd->Draw(1);
			}

			cmd->SetPointSize(1.0f);
			cmd->SetDepthTest(true);
			m_ProbeVisShader->UnBind();
		}

private:
	void BuildMaterialSSBO(Ref<Scene> scene) {
		std::vector<GPUMaterial> materials;

		for (auto [entityID,transform, meshrender] : scene->m_Registry.view<Component::Transform, Component::MeshRender>().each())
		{
			for (auto& mat : meshrender.materials)
			{
				if (!mat) continue;
				GPUMaterial gpu;
				gpu.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 0.5f);
				gpu.emission = glm::vec4(0.0f);
				gpu.diffuseHandle = 0;

				for (auto& v : mat->varies) {
					if (std::get<2>(v) == "albedo" || std::get<2>(v) == "color" || std::get<2>(v) == "baseColor") {
						if (std::get<1>(v) == ValueType::VEC3)
							gpu.albedo = glm::vec4(*(glm::vec3*)std::get<0>(v), 0.5f);
						else if (std::get<1>(v) == ValueType::FLOAT) {
							float vv = *(float*)std::get<0>(v);
							gpu.albedo = glm::vec4(vv, vv, vv, 0.5f);
						}
					}
				}

				// Extract diffuse texture handle (bindless)
				if (mat->texture && !mat->texture->GetPath().empty()) {
					unsigned int texID = (mat->texture ? (unsigned int)mat->texture->GetNativeID() : 0);
					if (texID && GLEW_ARB_bindless_texture) {
						GLuint64 handle = glGetTextureHandleARB(texID);
						if (handle) {
							glMakeTextureHandleResidentARB(handle);
							gpu.diffuseHandle = handle;
						}
					}
				}
				materials.push_back(gpu);
			}


		}

		if (materials.empty()) {
			GPUMaterial def;
			def.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 0.5f);
			def.emission = glm::vec4(0.0f);
			def.diffuseHandle = 0;
			materials.push_back(def);
		}
		m_MaterialSSBO = RHIBuffer::Create(BufferDesc{ (uint32_t)(materials.size() * sizeof(GPUMaterial)), BufferUsage::Storage, false, materials.data() });
	}

	void RebuildAtlases()
	{
		m_TotalProbes = GridSize.x * GridSize.y * GridSize.z;
		m_ProbesPerRow = DDGI::ComputeProbesPerRow(m_TotalProbes);

		int irradianceAtlasW = m_ProbesPerRow * 8;
		int irradianceAtlasH = ((m_TotalProbes + m_ProbesPerRow - 1) / m_ProbesPerRow) * 8;
		int depthAtlasW = m_ProbesPerRow * 16;
		int depthAtlasH = ((m_TotalProbes + m_ProbesPerRow - 1) / m_ProbesPerRow) * 16;

		m_IrradianceAtlas = RHIStorageImage::Create({ (uint32_t)irradianceAtlasW, (uint32_t)irradianceAtlasH, Format::RGBA16F });
		m_IrradianceAtlasPrev = RHIStorageImage::Create({ (uint32_t)irradianceAtlasW, (uint32_t)irradianceAtlasH, Format::RGBA16F });
		m_DepthAtlas = RHIStorageImage::Create({ (uint32_t)depthAtlasW, (uint32_t)depthAtlasH, Format::R16F });
		m_DepthAtlasPrev = RHIStorageImage::Create({ (uint32_t)depthAtlasW, (uint32_t)depthAtlasH, Format::R16F });

		// Allocate probe ray data SSBO
		m_ProbeRayDataSSBO = RHIBuffer::Create(BufferDesc{
			(uint32_t)(m_TotalProbes * sizeof(DDGI::GPUProbeRayData)),
			BufferUsage::Storage, false, nullptr });

		if (AutoPlaceGrid)
		{
			GridOrigin = currentcamera ? currentcamera->getpos()
				- glm::vec3(GridSize) * Spacing * 0.5f
				: glm::vec3(-16.0f, -4.0f, -16.0f);
		}

		m_ScrollOffset = glm::ivec3(0);
		m_CurrentProbeOffset = 0;
		m_FrameIdx = 0;
	}

	void UpdateProbeRayDataSSBO()
	{
		std::vector<DDGI::GPUProbeRayData> probeData;
		DDGI::BuildProbeRayData(probeData, GridSize, GridOrigin, Spacing, ProbeRadius, m_ScrollOffset);
		m_ProbeRayDataSSBO->Upload(probeData.data(),
			(uint32_t)(probeData.size() * sizeof(DDGI::GPUProbeRayData)));
	}

	void UpdateScrollOffset()
	{
		glm::vec3 camPos = currentcamera->getpos();
		glm::ivec3 newScroll(
			(int)std::floor(camPos.x / Spacing) - GridSize.x / 2,
			(int)std::floor(camPos.y / Spacing) - GridSize.y / 2,
			(int)std::floor(camPos.z / Spacing) - GridSize.z / 2);

		if (newScroll != m_ScrollOffset)
		{
			// Scroll: reset hysteresis for probes newly scrolled in
			// (simplified: swap clears history)
			m_ScrollOffset = newScroll;
			m_FrameIdx = 0;
		}
	}

	FrameBufferSpecification m_Spec;
	Ref<RHIShader> m_UpdateShader;
	Ref<BVHBuilder> m_BVHBuilder;

	// Probe atlas textures (ping-pong for temporal blending)
	Ref<RHIStorageImage> m_IrradianceAtlas;
	Ref<RHIStorageImage> m_IrradianceAtlasPrev;
	Ref<RHIStorageImage> m_DepthAtlas;
	Ref<RHIStorageImage> m_DepthAtlasPrev;

	// Probe position SSBO
	Ref<RHIBuffer> m_ProbeRayDataSSBO;

	// Grid state
	glm::ivec3 m_ScrollOffset = glm::ivec3(0);
	glm::ivec3 m_LastGridSize = glm::ivec3(8, 4, 8);
	int m_TotalProbes = 0;
	int m_ProbesPerRow = 0;
	int m_CurrentProbeOffset = 0;
	unsigned int m_FrameIdx = 0;
	Ref<RHIShader> m_ProbeVisShader;
	Ref<RHIBuffer> m_ProbeVisVB;
	Ref<RHIPipeline> m_ProbeVisPipeline;

	// Per-pass UBO + descriptor set.
	struct DDGIUBO
	{
		int32_t u_TotalProbes;
		int32_t u_ProbesPerRow;
		int32_t u_RaysPerProbe;
		int32_t u_ProbesPerUpdate;
		int32_t u_ProbeOffset;
		float   u_Hysteresis;
		float   u_FrameSeed;
		int32_t _pad0;                // pad to 32 (std140 vec4 align of trailing block)
	};
	static_assert(sizeof(DDGIUBO) == 32, "DDGIUBO must match std140 PerPass_DDGI");
	Ref<RHIBuffer> m_DDGIUBO;
	Ref<RHIDescriptorSet> m_DDGIDescriptorSet = RHIDescriptorSet::Create();

		void BuildProbeVisGeometry()
		{
			// Single point at origin — translated per-probe via MVP matrix
			float point[] = { 0.0f, 0.0f, 0.0f };
			m_ProbeVisVB = RHIBuffer::Create(BufferDesc{ (uint32_t)sizeof(point), BufferUsage::Vertex, false, point });
			VertexLayout layout;
			layout.stride = 3 * sizeof(float);
			layout.attributes = { { 0, VertexFormat::Float3, 0 } };
			PipelineDesc desc;
			desc.shader = m_ProbeVisShader;
			desc.vertexLayout = layout;
			desc.topology = PrimitiveTopology::Points;
			desc.cullMode = CullMode::None;
			desc.depthTest = false;
			m_ProbeVisPipeline = RHIPipeline::Create(desc);
		}
};
