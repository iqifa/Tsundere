#pragma once
#include <Pipeline/Passes/PassCommon.h>
#include "Pipeline/RenderPassRegistry.h"
#include "Pipeline/Passes/ForwardMeshRenderer.h"
#include "Scene/LocalLights.h"
#include <glm/gtc/constants.hpp>
#include <array>
#include <cmath>
#include <vector>

// ============================================================================
// Deferred LightPass Architecture Switch (for side-by-side learning comparison)
//   0 = TS_DEFERRED_LIGHT_MODE_UBER
//       Original single-pass super Uber-shader: fullscreen quad + loop over all
//       lights + per-pixel switch(shadingModel).
//   1 = TS_DEFERRED_LIGHT_MODE_UE_TIERED
//       UE-style complexity-tiered pipeline:
//       - GBuffer encodes shadingModel (Normal.w) + complexity tier (Emissive.a)
//         and writes low Stencil bits (0x00=Unlit/Sky, 0x01=Simple, 0x03=Complex).
//       - 8x8 Compute Tile Classification builds Simple / Complex / Unlit tile
//         lists and dispatches specialized Simple (no switch) vs Complex shaders
//         via DispatchIndirect for Base + Directional/Area lighting.
//       - Local lights (Point sphere / Spot cone) use Depth-Fail Stencil Light
//         Volume (bit 7) + separate Simple & Complex additive PSOs.
// ============================================================================
#ifndef TS_DEFERRED_LIGHT_MODE_UBER
#define TS_DEFERRED_LIGHT_MODE_UBER      0
#endif
#ifndef TS_DEFERRED_LIGHT_MODE_UE_TIERED
#define TS_DEFERRED_LIGHT_MODE_UE_TIERED 1
#endif
#ifndef TS_DEFERRED_LIGHT_MODE
#define TS_DEFERRED_LIGHT_MODE           TS_DEFERRED_LIGHT_MODE_UE_TIERED
#endif

// In UE_TIERED mode, controls how Directional + Base lighting runs:
//   0 = UE5-style 8x8 Compute Tile Classification + DispatchIndirect (Simple/Complex/Unlit)
//   1 = Fullscreen Complex Graphics pass (local lights still use Stencil Light Volume)
#ifndef TS_DEFERRED_DIRECTIONAL_FULLSCREEN_COMPLEX
#define TS_DEFERRED_DIRECTIONAL_FULLSCREEN_COMPLEX 0
#endif

struct DeferredLightDrawUBO
{
	glm::mat4 InvViewProjNoTrans;
	glm::mat4 View;
	glm::vec4 ViewPos;
	glm::vec2 ViewportSize;
	glm::vec2 ShadowMapSize;
	float Near;
	float Far;
	float LightCount;
	float pad;
	glm::vec4 Ambient;
};

struct DeferredGPULight
{
	glm::vec4 direc_ambient;
	glm::vec4 color_intens;
	glm::vec4 pos_type;
	glm::vec4 Param;
};

class DeferredLightPass : public RenderGraphPass
{
public:
	DeferredLightPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(std::move(scene)), m_FrameData(std::move(frameData))
	{
		constexpr uint32_t maxDrawObjects = 16;
		m_DeferredLightingUBO = RHIBuffer::Create(BufferDesc{
			sizeof(DeferredLightDrawUBO) * maxDrawObjects, BufferUsage::Uniform, true, nullptr });
		m_LightsData = RHIBuffer::Create(BufferDesc{
			m_LightCapacity * static_cast<uint32_t>(sizeof(DeferredGPULight)), BufferUsage::Storage, true, nullptr });

		unsigned char white[4] = { 255, 255, 255, 255 };
		m_DefaultTex = RHITexture2D::Create({ 1, 1, Format::RGBA8_UNORM, FilterMode::Linear, FilterMode::Linear,
											  WrapMode::ClampToEdge, WrapMode::ClampToEdge, false, white });

#if TS_DEFERRED_LIGHT_MODE == TS_DEFERRED_LIGHT_MODE_UBER || TS_DEFERRED_DIRECTIONAL_FULLSCREEN_COMPLEX
		InitFullscreenGraphicsResources();
#else
		InitTileComputeResources();
#endif
	}

	void Setup(RenderGraphBuilder& builder) override
	{
		m_Albedo = builder.ReadTexture("Gbuffer.SceneColor");
		m_Velocity = builder.ReadTexture("Gbuffer.Velocity");
		m_Dep_Stencil = builder.ReadTexture("Gbuffer.Depth");
		m_Emissive = builder.ReadTexture("Gbuffer.Emissive");
		m_Normal = builder.ReadTexture("Gbuffer.Normal");
		m_MetallicRoughness = builder.ReadTexture("Gbuffer.MetallicRoughness");

		RDGTextureDesc colorDesc;
		colorDesc.width = m_Width;
		colorDesc.height = m_Height;
		colorDesc.format = Format::RGBA16F;
		colorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Storage |
			TextureUsage::Sampled | TextureUsage::TransferSrc;

		m_DeferredLight = builder.CreateTexture("Light", colorDesc);
		builder.Export(m_DeferredLight);

#if TS_DEFERRED_LIGHT_MODE == TS_DEFERRED_LIGHT_MODE_UBER || TS_DEFERRED_DIRECTIONAL_FULLSCREEN_COMPLEX
		builder.SetColorOutput(0, m_DeferredLight, RGLoadOp::Clear, { 0, 0, 0, 0 });
		if (!m_DeferredPipeline)
		{
			m_DeferredPipelineDesc.descriptorSets = { m_DeferredDescriptorSet };
			m_DeferredPipeline = builder.CreatePipeline(
				m_DeferredLightShader, m_ScreenVertexLayout, &m_DeferredPipelineDesc);
			if (m_DeferredPipeline)
			{
				m_DeferredPipeline->SetupVertexFormat(ScreenVB);
				m_DeferredPipeline->SetupIndexBuffer(ScreenIB);
			}
		}
#else
		builder.WriteTexture(m_DeferredLight, RGAccess::WriteUAV);
		EnsureTileBuffers(m_Width, m_Height);
		EnsureComputePipelines();
#endif
	}

	void Execute(RenderGraphContext& context) override
	{
		auto& cmd = context.GetCmd();
		const auto& frameCamera = m_FrameData->Camera;

		std::vector<DeferredGPULight> lights;
		glm::vec3 ambient(0.0f);
		bool hasAmbient = false;
		for (const auto& light : GatherLocalLights(*m_Scene, true))
		{
			const int type = static_cast<int>(light.positionType.w + 0.5f);
			if (!hasAmbient && type == 0)
			{
				ambient = glm::vec3(light.colorIntensity) * light.colorIntensity.w * light.directionAmbient.w;
				hasAmbient = true;
			}
#if TS_DEFERRED_LIGHT_MODE == TS_DEFERRED_LIGHT_MODE_UE_TIERED
			// Point (1) and Spot (2) lights are evaluated by DeferredLocalLightPass via Stencil Light Volume.
			if (type == 1 || type == 2)
				continue;
#endif
			lights.push_back({ light.directionAmbient, light.colorIntensity, light.positionType, light.params });
		}

		if (lights.size() > m_LightCapacity)
		{
			m_LightCapacity = static_cast<uint32_t>(lights.size());
			m_LightsData = RHIBuffer::Create(BufferDesc{
				static_cast<uint32_t>(m_LightCapacity * sizeof(DeferredGPULight)), BufferUsage::Storage, true, nullptr });
		}
		if (!lights.empty())
			m_LightsData->Upload(lights.data(), static_cast<uint32_t>(lights.size() * sizeof(DeferredGPULight)));

		DeferredLightDrawUBO dlduo{};
		dlduo.InvViewProjNoTrans = glm::inverse(frameCamera.JitteredViewProj);
		dlduo.View = frameCamera.View;
		dlduo.ViewPos = glm::vec4(currentcamera->getpos(), 0.0f);
		dlduo.ViewportSize = { static_cast<float>(m_Width), static_cast<float>(m_Height) };
		dlduo.Near = currentcamera->getNear();
		dlduo.Far = currentcamera->getFar();
		dlduo.LightCount = static_cast<float>(lights.size());
		dlduo.Ambient = glm::vec4(ambient, 0.0f);
		m_DeferredLightingUBO->Upload(&dlduo, sizeof(dlduo));

		RHITexture2D* normalTex = context.GetTexture(m_Normal);
		RHITexture2D* albedoTex = context.GetTexture(m_Albedo);
		RHITexture2D* specularTex = context.GetTexture(m_MetallicRoughness);
		RHITexture2D* depthTex = context.GetTexture(m_Dep_Stencil);
		RHITexture2D* emissiveTex = context.GetTexture(m_Emissive);
		RHITexture2D* velocityTex = context.GetTexture(m_Velocity);

#if TS_DEFERRED_LIGHT_MODE == TS_DEFERRED_LIGHT_MODE_UBER || TS_DEFERRED_DIRECTIONAL_FULLSCREEN_COMPLEX
		cmd.SetDepthTest(false);
		cmd.SetDepthFunc(CompareOp::Always);

		m_DeferredDescriptorSet->Reset();
		m_DeferredDescriptorSet->BindUniformBuffer(0, m_DeferredLightingUBO, sizeof(DeferredLightDrawUBO));
		m_DeferredDescriptorSet->BindStorageBuffer(1, m_LightsData);
		m_DeferredDescriptorSet->BindTexture(10, normalTex ? normalTex : m_DefaultTex.get(), 10);
		m_DeferredDescriptorSet->BindTexture(11, albedoTex ? albedoTex : m_DefaultTex.get(), 11);
		m_DeferredDescriptorSet->BindTexture(12, specularTex ? specularTex : m_DefaultTex.get(), 12);
		m_DeferredDescriptorSet->BindTexture(13, depthTex ? depthTex : m_DefaultTex.get(), 13);
		m_DeferredDescriptorSet->BindTexture(14, emissiveTex ? emissiveTex : m_DefaultTex.get(), 14);
		m_DeferredDescriptorSet->BindTexture(15, velocityTex ? velocityTex : m_DefaultTex.get(), 15);
		m_DeferredDescriptorSet->Apply(0);

		cmd.BindPipeline(m_DeferredPipeline);
		cmd.BindVertexBuffer(ScreenVB, 0);
		cmd.BindIndexBuffer(ScreenIB);
		const uint32_t dynamicOffset = 0;
		cmd.BindDescriptorSet(m_DeferredDescriptorSet, 0, &dynamicOffset, 1);
		cmd.DrawIndexed(6);
		m_DeferredPipeline->Unbind();
#else
		RHITexture2D* lightOut = context.GetTexture(m_DeferredLight);
		if (!lightOut || !m_TileClassifyPipeline || !m_TileLightSimplePipeline ||
			!m_TileLightComplexPipeline || !m_TileLightUnlitPipeline)
			return;

		const uint32_t tilesX = (m_Width + 7u) / 8u;
		const uint32_t tilesY = (m_Height + 7u) / 8u;
		EnsureTileBuffers(m_Width, m_Height);

		const TileClassifyUBO classifyUBO{ { m_Width, m_Height }, { tilesX, tilesY } };
		m_TileClassifyUBO->Upload(&classifyUBO, sizeof(classifyUBO));

		const TileIndirectArgsData initialArgs{
			{ 0u, 1u, 1u, 0u },
			{ 0u, 1u, 1u, 0u },
			{ 0u, 1u, 1u, 0u },
			{ 0u, tilesX * tilesY, 0u, 0u }
		};
		m_IndirectArgsBuffer->Upload(&initialArgs, sizeof(initialArgs));

		// 1. 8x8 Compute Tile Classification + sky clear
		m_TileClassifyDescSet->Reset();
		m_TileClassifyDescSet->BindUniformBuffer(0, m_TileClassifyUBO);
		m_TileClassifyDescSet->BindStorageBuffer(1, m_IndirectArgsBuffer);
		m_TileClassifyDescSet->BindStorageBuffer(2, m_SimpleTileBuffer);
		m_TileClassifyDescSet->BindStorageBuffer(3, m_ComplexTileBuffer);
		m_TileClassifyDescSet->BindStorageBuffer(4, m_UnlitTileBuffer);
		m_TileClassifyDescSet->BindStorageTexture(5, lightOut, ImageAccess::WriteOnly, 5);
		m_TileClassifyDescSet->BindTexture(10, normalTex ? normalTex : m_DefaultTex.get(), 10);
		m_TileClassifyDescSet->BindTexture(13, depthTex ? depthTex : m_DefaultTex.get(), 13);
		m_TileClassifyDescSet->BindTexture(14, emissiveTex ? emissiveTex : m_DefaultTex.get(), 14);
		m_TileClassifyDescSet->Apply(0);

		cmd.BindPipeline(m_TileClassifyPipeline);
		cmd.BindDescriptorSet(m_TileClassifyDescSet, 0);
		cmd.Dispatch(tilesX, tilesY, 1);

		cmd.ResourceBarrier(BarrierFlags::StorageBuffer | BarrierFlags::IndirectCommand | BarrierFlags::ShaderImage);

		// 2. DispatchIndirect specialized shaders per tile bucket
		m_TileLightDescSet->Reset();
		m_TileLightDescSet->BindUniformBuffer(0, m_DeferredLightingUBO);
		m_TileLightDescSet->BindStorageBuffer(1, m_LightsData);
		m_TileLightDescSet->BindStorageBuffer(2, m_SimpleTileBuffer);
		m_TileLightDescSet->BindStorageBuffer(3, m_ComplexTileBuffer);
		m_TileLightDescSet->BindStorageBuffer(4, m_UnlitTileBuffer);
		m_TileLightDescSet->BindStorageTexture(5, lightOut, ImageAccess::WriteOnly, 5);
		m_TileLightDescSet->BindTexture(10, normalTex ? normalTex : m_DefaultTex.get(), 10);
		m_TileLightDescSet->BindTexture(11, albedoTex ? albedoTex : m_DefaultTex.get(), 11);
		m_TileLightDescSet->BindTexture(12, specularTex ? specularTex : m_DefaultTex.get(), 12);
		m_TileLightDescSet->BindTexture(13, depthTex ? depthTex : m_DefaultTex.get(), 13);
		m_TileLightDescSet->BindTexture(14, emissiveTex ? emissiveTex : m_DefaultTex.get(), 14);
		m_TileLightDescSet->BindTexture(15, velocityTex ? velocityTex : m_DefaultTex.get(), 15);
		m_TileLightDescSet->Apply(0);

		// Unlit-only tiles (offset 32 bytes)
		cmd.BindPipeline(m_TileLightUnlitPipeline);
		cmd.BindDescriptorSet(m_TileLightDescSet, 0);
		cmd.DispatchIndirect(m_IndirectArgsBuffer, 32);

		// Simple Lit tiles (offset 0 bytes, zero shading-model switch)
		cmd.BindPipeline(m_TileLightSimplePipeline);
		cmd.BindDescriptorSet(m_TileLightDescSet, 0);
		cmd.DispatchIndirect(m_IndirectArgsBuffer, 0);

		// Complex Lit tiles (offset 16 bytes, ClearCoat/Subsurface switch)
		cmd.BindPipeline(m_TileLightComplexPipeline);
		cmd.BindDescriptorSet(m_TileLightDescSet, 0);
		cmd.DispatchIndirect(m_IndirectArgsBuffer, 16);
#endif
	}

	const char* GetName() const override { return "DeferredLight"; }

	void ValidateShaderLayout(const Ref<RHIShader>& shader) const
	{
		if (!shader)
			return;

		const ShaderReflection& reflection = shader->GetReflection();
		if (const ShaderBlock* ubo = reflection.FindUniformBlock(0); ubo && ubo->Size != sizeof(DeferredLightDrawUBO))
		{
			Error_Core("[{} Pass]: DeferredLightDrawUBO is {} bytes but shader '{}' block '{}' is {} bytes",
				GetName(), sizeof(DeferredLightDrawUBO), shader->GetPath(), ubo->Name, ubo->Size);
		}

		const ShaderBlock* ssbo = reflection.FindStorageBlock(1);
		if (ssbo && !ssbo->Members.empty() && ssbo->Members[0].ArrayStride != sizeof(DeferredGPULight))
		{
			Error_Core("[{} Pass]: DeferredGPULight is {} bytes but shader '{}' array '{}' has stride {}",
				GetName(), sizeof(DeferredGPULight), shader->GetPath(), ssbo->Members[0].Name, ssbo->Members[0].ArrayStride);
		}
	}

	void SetViewportSize(uint32_t width, uint32_t height) override
	{
		m_Width = width;
		m_Height = height;
	}

private:
	struct TileClassifyUBO
	{
		glm::uvec2 ViewportSize;
		glm::uvec2 TileCount;
	};

	struct TileIndirectArgsData
	{
		glm::uvec4 simpleArgs;
		glm::uvec4 complexArgs;
		glm::uvec4 unlitArgs;
		glm::uvec4 stats;
	};

	void InitFullscreenGraphicsResources()
	{
#if TS_DEFERRED_LIGHT_MODE == TS_DEFERRED_LIGHT_MODE_UBER
		m_DeferredLightShader = RHIShader::Create("res/shaders/DeferredLight.shader");
#else
		m_DeferredLightShader = RHIShader::Create("res/shaders/DeferredDirectionalComplex.shader");
#endif
		ValidateShaderLayout(m_DeferredLightShader);

		m_DeferredDescriptorSet = RHIDescriptorSet::Create();
		if (m_DeferredDescriptorSet)
		{
			m_DeferredDescriptorSet->BindUniformBuffer(0, m_DeferredLightingUBO, sizeof(DeferredLightDrawUBO));
			m_DeferredDescriptorSet->BindStorageBuffer(1, m_LightsData);
			m_DeferredDescriptorSet->BindTexture(10, m_DefaultTex, 10);
			m_DeferredDescriptorSet->BindTexture(11, m_DefaultTex, 11);
			m_DeferredDescriptorSet->BindTexture(12, m_DefaultTex, 12);
			m_DeferredDescriptorSet->BindTexture(13, m_DefaultTex, 13);
			m_DeferredDescriptorSet->BindTexture(14, m_DefaultTex, 14);
			m_DeferredDescriptorSet->BindTexture(15, m_DefaultTex, 15);
			m_DeferredDescriptorSet->MarkBindingAsDynamic(0);
		}
		else
		{
			Error_Core("[{} Pass]: m_DeferredDescriptorSet is Null", GetName());
		}

		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f
		};
		unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };

		ScreenVB = RHIBuffer::Create({ static_cast<uint32_t>(sizeof(quadVertices)), BufferUsage::Vertex, false, quadVertices });
		ScreenIB = RHIBuffer::Create({ static_cast<uint32_t>(sizeof(quadIndices)), BufferUsage::Index, false, quadIndices });

		VertexLayout screenVertexLayout;
		screenVertexLayout.stride = 4 * sizeof(float);
		screenVertexLayout.attributes = {
			{ 0, VertexFormat::Float2, 0, 0 },
			{ 1, VertexFormat::Float2, 2 * sizeof(float), 0 },
		};

		PipelineDesc pipeDesc;
		pipeDesc.shader = m_DeferredLightShader;
		pipeDesc.vertexLayout = screenVertexLayout;
		pipeDesc.topology = PrimitiveTopology::Triangles;
		pipeDesc.cullMode = CullMode::Back;
		pipeDesc.depthTest = false;
		pipeDesc.depthWrite = false;
		pipeDesc.srcBlend = BlendFactor::One;
		pipeDesc.dstBlend = BlendFactor::Zero;

		m_DeferredPipelineDesc = pipeDesc;
		m_ScreenVertexLayout = screenVertexLayout;
	}

	void InitTileComputeResources()
	{
		m_TileClassifyShader = RHIShader::CreateCompute("res/shaders/DeferredTileClassify.shader");
		m_TileLightUnlitShader = RHIShader::CreateCompute("res/shaders/DeferredTileLightUnlit.shader");
		m_TileLightSimpleShader = RHIShader::CreateCompute("res/shaders/DeferredTileLightSimple.shader");
		m_TileLightComplexShader = RHIShader::CreateCompute("res/shaders/DeferredTileLightComplex.shader");
		ValidateShaderLayout(m_TileLightSimpleShader);
		ValidateShaderLayout(m_TileLightComplexShader);

		m_TileClassifyUBO = RHIBuffer::Create(BufferDesc{ sizeof(TileClassifyUBO), BufferUsage::Uniform, true, nullptr });
		m_IndirectArgsBuffer = RHIBuffer::Create(BufferDesc{ sizeof(TileIndirectArgsData), BufferUsage::Storage, true, nullptr });
		EnsureTileBuffers(m_Width, m_Height);

		m_TileClassifyDescSet = RHIDescriptorSet::Create();
		if (m_TileClassifyDescSet)
		{
			m_TileClassifyDescSet->BindUniformBuffer(0, m_TileClassifyUBO);
			m_TileClassifyDescSet->BindStorageBuffer(1, m_IndirectArgsBuffer);
			m_TileClassifyDescSet->BindStorageBuffer(2, m_SimpleTileBuffer);
			m_TileClassifyDescSet->BindStorageBuffer(3, m_ComplexTileBuffer);
			m_TileClassifyDescSet->BindStorageBuffer(4, m_UnlitTileBuffer);
			m_TileClassifyDescSet->BindStorageTexture(5, m_DefaultTex.get(), ImageAccess::WriteOnly, 5);
			m_TileClassifyDescSet->BindTexture(10, m_DefaultTex, 10);
			m_TileClassifyDescSet->BindTexture(13, m_DefaultTex, 13);
			m_TileClassifyDescSet->BindTexture(14, m_DefaultTex, 14);
		}

		m_TileLightDescSet = RHIDescriptorSet::Create();
		if (m_TileLightDescSet)
		{
			m_TileLightDescSet->BindUniformBuffer(0, m_DeferredLightingUBO);
			m_TileLightDescSet->BindStorageBuffer(1, m_LightsData);
			m_TileLightDescSet->BindStorageBuffer(2, m_SimpleTileBuffer);
			m_TileLightDescSet->BindStorageBuffer(3, m_ComplexTileBuffer);
			m_TileLightDescSet->BindStorageBuffer(4, m_UnlitTileBuffer);
			m_TileLightDescSet->BindStorageTexture(5, m_DefaultTex.get(), ImageAccess::WriteOnly, 5);
			m_TileLightDescSet->BindTexture(10, m_DefaultTex, 10);
			m_TileLightDescSet->BindTexture(11, m_DefaultTex, 11);
			m_TileLightDescSet->BindTexture(12, m_DefaultTex, 12);
			m_TileLightDescSet->BindTexture(13, m_DefaultTex, 13);
			m_TileLightDescSet->BindTexture(14, m_DefaultTex, 14);
			m_TileLightDescSet->BindTexture(15, m_DefaultTex, 15);
		}
	}

	void EnsureTileBuffers(uint32_t width, uint32_t height)
	{
		const uint32_t tilesX = (width + 7u) / 8u;
		const uint32_t tilesY = (height + 7u) / 8u;
		const uint32_t requiredTiles = std::max<uint32_t>(tilesX * tilesY, 1u);
		if (requiredTiles <= m_TileBufferCapacity && m_SimpleTileBuffer && m_ComplexTileBuffer && m_UnlitTileBuffer)
			return;
		m_TileBufferCapacity = requiredTiles;
		const uint32_t bytes = m_TileBufferCapacity * static_cast<uint32_t>(sizeof(uint32_t));
		m_SimpleTileBuffer = RHIBuffer::Create(BufferDesc{ bytes, BufferUsage::Storage, false, nullptr });
		m_ComplexTileBuffer = RHIBuffer::Create(BufferDesc{ bytes, BufferUsage::Storage, false, nullptr });
		m_UnlitTileBuffer = RHIBuffer::Create(BufferDesc{ bytes, BufferUsage::Storage, false, nullptr });
	}

	void EnsureComputePipelines()
	{
		auto makeCompute = [](const Ref<RHIShader>& shader, const Ref<RHIDescriptorSet>& set) -> Ref<RHIPipeline>
		{
			if (!shader || !set)
				return nullptr;
			PipelineDesc desc;
			desc.shader = shader;
			desc.isCompute = true;
			desc.descriptorSets = { set };
			return RHIPipeline::Create(desc);
		};
		if (!m_TileClassifyPipeline)
			m_TileClassifyPipeline = makeCompute(m_TileClassifyShader, m_TileClassifyDescSet);
		if (!m_TileLightUnlitPipeline)
			m_TileLightUnlitPipeline = makeCompute(m_TileLightUnlitShader, m_TileLightDescSet);
		if (!m_TileLightSimplePipeline)
			m_TileLightSimplePipeline = makeCompute(m_TileLightSimpleShader, m_TileLightDescSet);
		if (!m_TileLightComplexPipeline)
			m_TileLightComplexPipeline = makeCompute(m_TileLightComplexShader, m_TileLightDescSet);
	}

	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;

	Ref<RHIBuffer> m_DeferredLightingUBO;
	Ref<RHIBuffer> m_LightsData;
	uint32_t m_LightCapacity = 128;

	Ref<RHITexture2D> m_DefaultTex;

	// Graphics fullscreen path (Uber or optional Fullscreen Complex Directional)
	Ref<RHIShader> m_DeferredLightShader;
	Ref<RHIDescriptorSet> m_DeferredDescriptorSet;
	Ref<RHIPipeline> m_DeferredPipeline;
	PipelineDesc m_DeferredPipelineDesc;
	VertexLayout m_ScreenVertexLayout;
	Ref<RHIBuffer> ScreenVB;
	Ref<RHIBuffer> ScreenIB;

	// UE5-style Compute Tile Classification + DispatchIndirect path
	Ref<RHIShader> m_TileClassifyShader;
	Ref<RHIShader> m_TileLightUnlitShader;
	Ref<RHIShader> m_TileLightSimpleShader;
	Ref<RHIShader> m_TileLightComplexShader;
	Ref<RHIBuffer> m_TileClassifyUBO;
	Ref<RHIBuffer> m_IndirectArgsBuffer;
	Ref<RHIBuffer> m_SimpleTileBuffer;
	Ref<RHIBuffer> m_ComplexTileBuffer;
	Ref<RHIBuffer> m_UnlitTileBuffer;
	uint32_t m_TileBufferCapacity = 0;
	Ref<RHIDescriptorSet> m_TileClassifyDescSet;
	Ref<RHIDescriptorSet> m_TileLightDescSet;
	Ref<RHIPipeline> m_TileClassifyPipeline;
	Ref<RHIPipeline> m_TileLightUnlitPipeline;
	Ref<RHIPipeline> m_TileLightSimplePipeline;
	Ref<RHIPipeline> m_TileLightComplexPipeline;

	RGTextureHandle m_Dep_Stencil;
	RGTextureHandle m_DeferredLight;
	RGTextureHandle m_Albedo;
	RGTextureHandle m_Velocity;
	RGTextureHandle m_Normal;
	RGTextureHandle m_MetallicRoughness;
	RGTextureHandle m_Emissive;

	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;
};

REGISTER_RENDER_PASS(DeferredLightPass, "DeferredLight", "Base", 12, false)

// ============================================================================
// DeferredLocalLightPass (active under TS_DEFERRED_LIGHT_MODE_UE_TIERED):
// Evaluates Point (unit sphere) and Spot (unit cone) local lights using
// two-pass Depth-Fail Stencil Light Volume (bit 7) + separate Simple (0x81)
// and Complex (0x83) additive PSOs.
// ============================================================================
class DeferredLocalLightPass : public RenderGraphPass
{
public:
	DeferredLocalLightPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(std::move(scene)), m_FrameData(std::move(frameData))
	{
		m_LightingUBO = RHIBuffer::Create(BufferDesc{
			sizeof(DeferredLightDrawUBO), BufferUsage::Uniform, true, nullptr });
		m_LocalLightsData = RHIBuffer::Create(BufferDesc{
			m_LightCapacity * static_cast<uint32_t>(sizeof(DeferredGPULight)), BufferUsage::Storage, true, nullptr });

		unsigned char white[4] = { 255, 255, 255, 255 };
		m_DefaultTex = RHITexture2D::Create({ 1, 1, Format::RGBA8_UNORM, FilterMode::Linear, FilterMode::Linear,
											  WrapMode::ClampToEdge, WrapMode::ClampToEdge, false, white });

		m_StencilShader = RHIShader::Create("res/shaders/DeferredLightVolumeStencil.shader");
		m_LocalSimpleShader = RHIShader::Create("res/shaders/DeferredLocalLightSimple.shader");
		m_LocalComplexShader = RHIShader::Create("res/shaders/DeferredLocalLightComplex.shader");

		m_DescriptorSet = RHIDescriptorSet::Create();
		if (m_DescriptorSet)
		{
			m_DescriptorSet->BindUniformBuffer(0, m_LightingUBO);
			m_DescriptorSet->BindStorageBuffer(1, m_LocalLightsData);
			m_DescriptorSet->BindTexture(10, m_DefaultTex, 10);
			m_DescriptorSet->BindTexture(11, m_DefaultTex, 11);
			m_DescriptorSet->BindTexture(12, m_DefaultTex, 12);
			m_DescriptorSet->BindTexture(13, m_DefaultTex, 13);
			m_DescriptorSet->BindTexture(14, m_DefaultTex, 14);
			m_DescriptorSet->BindTexture(15, m_DefaultTex, 15);
		}

		BuildVolumeMeshes();
	}

	const char* GetName() const override { return "DeferredLocalLight"; }

	void SetViewportSize(uint32_t width, uint32_t height) override
	{
		m_Width = width;
		m_Height = height;
	}

	void Setup(RenderGraphBuilder& builder) override
	{
		m_Normal = builder.ReadTexture("Gbuffer.Normal");
		m_Albedo = builder.ReadTexture("Gbuffer.SceneColor");
		m_MetallicRoughness = builder.ReadTexture("Gbuffer.MetallicRoughness");
		m_Emissive = builder.ReadTexture("Gbuffer.Emissive");
		m_Velocity = builder.ReadTexture("Gbuffer.Velocity");
		m_Depth = builder.ReadTexture("Gbuffer.Depth", RGAccess::CopySrc);
		m_Light = builder.ReadTexture("DeferredLight.Light", RGAccess::RenderTarget);

		// Copy Gbuffer.Depth into a sampled texture so fragment shaders reconstruct
		// world position without a GL feedback loop or Vulkan layout conflict while
		// Gbuffer.Depth is bound as the live Depth-Stencil attachment.
		RDGTextureDesc depthCopyDesc;
		depthCopyDesc.width = m_Width;
		depthCopyDesc.height = m_Height;
		depthCopyDesc.format = Format::D24_UNORM_S8_UINT;
		depthCopyDesc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled | TextureUsage::TransferDst;
		depthCopyDesc.minFilter = FilterMode::Nearest;
		depthCopyDesc.magFilter = FilterMode::Nearest;
		m_DepthCopy = builder.CreateTexture("DepthCopy", depthCopyDesc);
		builder.WriteTexture(m_DepthCopy, RGAccess::CopyDst);
		builder.ReadTexture("DepthCopy", RGAccess::ReadSRV);
		builder.DescribeTextureCopy(m_Depth, m_DepthCopy);

		builder.SetColorOutput(0, m_Light, RGLoadOp::Load);
		builder.SetDepthOutput(m_Depth, RGLoadOp::Load, 1.0f, false);
		builder.SetStencilOps(RGLoadOp::Load, 0);

		VertexLayout volumeLayout;
		volumeLayout.stride = 3 * sizeof(float);
		volumeLayout.attributes = { { 0, VertexFormat::Float3, 0, 0 } };

		if (!m_VolumeStencilPipeline)
		{
			PipelineDesc desc;
			desc.shader = m_StencilShader;
			desc.vertexLayout = volumeLayout;
			desc.topology = PrimitiveTopology::Triangles;
			desc.cullMode = CullMode::None;
			desc.depthTest = true;
			desc.depthWrite = false;
			desc.depthOp = CompareOp::Less;
			desc.colorWriteEnable = false;
			desc.pushConstantSize = sizeof(LightVolumePushConstants);
			// Depth-Fail (Carmack's Reverse) on bit 7, restricted to Lit pixels (bit 0 == 1).
			desc.stencilTest = true;
			desc.frontStencil = { StencilOp::Keep, StencilOp::Keep, StencilOp::Invert,
				CompareOp::Equal, 0x01, 0x80, 0x01 };
			desc.backStencil = desc.frontStencil;
			m_VolumeStencilPipeline = builder.CreatePipeline(m_StencilShader, volumeLayout, &desc);
		}

		auto buildVolumeShadingPipe = [&](const Ref<RHIShader>& shader, uint32_t stencilRef, bool fullscreenFallback)
		{
			PipelineDesc desc;
			desc.shader = shader;
			desc.vertexLayout = volumeLayout;
			desc.topology = PrimitiveTopology::Triangles;
			desc.cullMode = fullscreenFallback ? CullMode::None : CullMode::Front;
			desc.depthTest = false;
			desc.depthWrite = false;
			desc.colorWriteEnable = true;
			desc.srcBlend = BlendFactor::One;
			desc.dstBlend = BlendFactor::One;
			desc.pushConstantSize = sizeof(LightVolumePushConstants);
			desc.descriptorSets = { m_DescriptorSet };
			desc.stencilTest = true;
			if (fullscreenFallback)
			{
				desc.frontStencil = { StencilOp::Keep, StencilOp::Keep, StencilOp::Keep,
					CompareOp::Equal, 0x03, 0x00, stencilRef & 0x03u };
			}
			else
			{
				// Match (bit7=1, complexity, Lit=1) and clear bit 7 on passing pixels only.
				desc.frontStencil = { StencilOp::Keep, StencilOp::Zero, StencilOp::Keep,
					CompareOp::Equal, 0x83, 0x80, stencilRef };
			}
			desc.backStencil = desc.frontStencil;
			return builder.CreatePipeline(shader, volumeLayout, &desc);
		};

		if (!m_LocalSimplePipeline)
			m_LocalSimplePipeline = buildVolumeShadingPipe(m_LocalSimpleShader, 0x81, false);
		if (!m_LocalComplexPipeline)
			m_LocalComplexPipeline = buildVolumeShadingPipe(m_LocalComplexShader, 0x83, false);
		if (!m_FallbackSimplePipeline)
			m_FallbackSimplePipeline = buildVolumeShadingPipe(m_LocalSimpleShader, 0x01, true);
		if (!m_FallbackComplexPipeline)
			m_FallbackComplexPipeline = buildVolumeShadingPipe(m_LocalComplexShader, 0x03, true);
	}

	void BeforeRender(RenderGraphContext& context) override
	{
		context.GetCmd().CopyTexture(context.GetTexture(m_Depth), context.GetTexture(m_DepthCopy));
	}

	void Execute(RenderGraphContext& context) override
	{
		std::vector<DeferredGPULight> localLights;
		for (const auto& light : GatherLocalLights(*m_Scene, false))
		{
			const int type = static_cast<int>(light.positionType.w + 0.5f);
			if ((type == 1 || type == 2) && light.colorIntensity.w > 0.0f && light.params.x > 0.0f)
				localLights.push_back({ light.directionAmbient, light.colorIntensity, light.positionType, light.params });
		}
		if (localLights.empty())
			return;

		if (localLights.size() > m_LightCapacity)
		{
			m_LightCapacity = static_cast<uint32_t>(localLights.size());
			m_LocalLightsData = RHIBuffer::Create(BufferDesc{
				static_cast<uint32_t>(m_LightCapacity * sizeof(DeferredGPULight)), BufferUsage::Storage, true, nullptr });
		}
		m_LocalLightsData->Upload(localLights.data(), static_cast<uint32_t>(localLights.size() * sizeof(DeferredGPULight)));

		const auto& frameCamera = m_FrameData->Camera;
		const glm::vec3 camPos = currentcamera->getpos();
		const float camFar = currentcamera->getFar();

		DeferredLightDrawUBO ubo{};
		ubo.InvViewProjNoTrans = glm::inverse(frameCamera.JitteredViewProj);
		ubo.View = frameCamera.View;
		ubo.ViewPos = glm::vec4(camPos, 0.0f);
		ubo.ViewportSize = { static_cast<float>(m_Width), static_cast<float>(m_Height) };
		ubo.Near = currentcamera->getNear();
		ubo.Far = camFar;
		ubo.LightCount = static_cast<float>(localLights.size());
		m_LightingUBO->Upload(&ubo, sizeof(ubo));

		RHITexture2D* normalTex = context.GetTexture(m_Normal);
		RHITexture2D* albedoTex = context.GetTexture(m_Albedo);
		RHITexture2D* mrTex = context.GetTexture(m_MetallicRoughness);
		RHITexture2D* depthCopyTex = context.GetTexture(m_DepthCopy);
		RHITexture2D* emissiveTex = context.GetTexture(m_Emissive);
		RHITexture2D* velocityTex = context.GetTexture(m_Velocity);

		m_DescriptorSet->Reset();
		m_DescriptorSet->BindUniformBuffer(0, m_LightingUBO);
		m_DescriptorSet->BindStorageBuffer(1, m_LocalLightsData);
		m_DescriptorSet->BindTexture(10, normalTex ? normalTex : m_DefaultTex.get(), 10);
		m_DescriptorSet->BindTexture(11, albedoTex ? albedoTex : m_DefaultTex.get(), 11);
		m_DescriptorSet->BindTexture(12, mrTex ? mrTex : m_DefaultTex.get(), 12);
		m_DescriptorSet->BindTexture(13, depthCopyTex ? depthCopyTex : m_DefaultTex.get(), 13);
		m_DescriptorSet->BindTexture(14, emissiveTex ? emissiveTex : m_DefaultTex.get(), 14);
		m_DescriptorSet->BindTexture(15, velocityTex ? velocityTex : m_DefaultTex.get(), 15);
		m_DescriptorSet->Apply(0);

		auto& cmd = context.GetCmd();
		const bool drawComplex = !m_FrameData || m_FrameData->HasComplexGBufferPixels;

		for (uint32_t i = 0; i < static_cast<uint32_t>(localLights.size()); ++i)
		{
			const DeferredGPULight& light = localLights[i];
			const int type = static_cast<int>(light.pos_type.w + 0.5f);
			const glm::vec3 pos = glm::vec3(light.pos_type);
			const float radius = light.Param.x;

			// If the light volume could clip against the camera far plane, fall back
			// to a fullscreen quad still split into Simple / Complex via low Stencil bits.
			const bool useFullscreenFallback = (glm::length(camPos - pos) + radius * 1.1f) >= camFar * 0.95f;
			if (useFullscreenFallback)
			{
				const LightVolumePushConstants pc{ glm::mat4(1.0f), static_cast<int32_t>(i), 0, 0, 0 };
				cmd.BindPipeline(m_FallbackSimplePipeline);
				cmd.BindVertexBuffer(m_QuadVB, 0);
				cmd.BindIndexBuffer(m_QuadIB);
				cmd.BindDescriptorSet(m_DescriptorSet, 0);
				cmd.PushConstants(&pc, sizeof(pc));
				cmd.DrawIndexed(6);

				if (drawComplex)
				{
					cmd.BindPipeline(m_FallbackComplexPipeline);
					cmd.BindVertexBuffer(m_QuadVB, 0);
					cmd.BindIndexBuffer(m_QuadIB);
					cmd.BindDescriptorSet(m_DescriptorSet, 0);
					cmd.PushConstants(&pc, sizeof(pc));
					cmd.DrawIndexed(6);
				}
				continue;
			}

			bool useCone = false;
			glm::mat4 model(1.0f);
			if (type == 2)
			{
				const glm::vec3 rawDir = glm::vec3(light.direc_ambient);
				const float inner = glm::clamp(light.Param.z, 0.0f, 89.0f);
				const float outer = glm::clamp(light.Param.w, inner, 89.0f);
				if (glm::dot(rawDir, rawDir) > 1e-8f && outer <= 80.0f)
				{
					useCone = true;
					const glm::vec3 dir = glm::normalize(rawDir);
					const glm::vec3 ref = std::abs(dir.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
					const glm::vec3 right = glm::normalize(glm::cross(ref, dir));
					const glm::vec3 up = glm::cross(dir, right);
					const float length = radius * 1.05f;
					const float baseRadius = length * std::tan(glm::radians(outer)) * 1.08f;
					model[0] = glm::vec4(right * baseRadius, 0.0f);
					model[1] = glm::vec4(up * baseRadius, 0.0f);
					model[2] = glm::vec4(dir * length, 0.0f);
					model[3] = glm::vec4(pos, 1.0f);
				}
			}
			if (!useCone)
			{
				model = glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(radius * 1.05f));
			}

			const Ref<RHIBuffer>& vb = useCone ? m_ConeVB : m_SphereVB;
			const Ref<RHIBuffer>& ib = useCone ? m_ConeIB : m_SphereIB;
			const uint32_t indexCount = useCone ? m_ConeIndexCount : m_SphereIndexCount;

			const LightVolumePushConstants pc{ frameCamera.JitteredViewProj * model, static_cast<int32_t>(i), 0, 0, 0 };

			// Sub-pass A: Depth-Fail Stencil marking (bit 7 on Lit pixels only)
			cmd.BindPipeline(m_VolumeStencilPipeline);
			cmd.BindVertexBuffer(vb, 0);
			cmd.BindIndexBuffer(ib);
			cmd.PushConstants(&pc, sizeof(pc));
			cmd.DrawIndexed(indexCount);

			// Sub-pass B: Simple Lit pixels inside volume (Stencil 0x81 -> clears bit 7)
			cmd.BindPipeline(m_LocalSimplePipeline);
			cmd.BindVertexBuffer(vb, 0);
			cmd.BindIndexBuffer(ib);
			cmd.BindDescriptorSet(m_DescriptorSet, 0);
			cmd.PushConstants(&pc, sizeof(pc));
			cmd.DrawIndexed(indexCount);

			// Sub-pass C: Complex Lit pixels inside volume (Stencil 0x83 -> clears bit 7)
			if (drawComplex)
			{
				cmd.BindPipeline(m_LocalComplexPipeline);
				cmd.BindVertexBuffer(vb, 0);
				cmd.BindIndexBuffer(ib);
				cmd.BindDescriptorSet(m_DescriptorSet, 0);
				cmd.PushConstants(&pc, sizeof(pc));
				cmd.DrawIndexed(indexCount);
			}
		}

		m_LocalSimplePipeline->Unbind();
	}

private:
	struct LightVolumePushConstants
	{
		glm::mat4 ModelViewProj;
		int32_t LightIndex;
		int32_t _pad0;
		int32_t _pad1;
		int32_t _pad2;
	};
	static_assert(sizeof(LightVolumePushConstants) == 80);

	void BuildVolumeMeshes()
	{
		// 1. Unit UV Sphere (radius 1, outward CCW winding)
		{
			constexpr uint32_t stacks = 12;
			constexpr uint32_t slices = 16;
			std::vector<float> verts;
			std::vector<uint32_t> indices;
			for (uint32_t y = 0; y <= stacks; ++y)
			{
				const float v = static_cast<float>(y) / static_cast<float>(stacks);
				const float phi = v * glm::pi<float>();
				const float sinPhi = std::sin(phi);
				const float cosPhi = std::cos(phi);
				for (uint32_t x = 0; x <= slices; ++x)
				{
					const float u = static_cast<float>(x) / static_cast<float>(slices);
					const float theta = u * glm::two_pi<float>();
					verts.push_back(sinPhi * std::cos(theta));
					verts.push_back(cosPhi);
					verts.push_back(sinPhi * std::sin(theta));
				}
			}
			const uint32_t ring = slices + 1;
			for (uint32_t y = 0; y < stacks; ++y)
			{
				for (uint32_t x = 0; x < slices; ++x)
				{
					const uint32_t i0 = y * ring + x;
					const uint32_t i1 = i0 + 1;
					const uint32_t i2 = (y + 1) * ring + x;
					const uint32_t i3 = i2 + 1;
					indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
					indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
				}
			}
			m_SphereIndexCount = static_cast<uint32_t>(indices.size());
			m_SphereVB = RHIBuffer::Create({ static_cast<uint32_t>(verts.size() * sizeof(float)), BufferUsage::Vertex, false, verts.data() });
			m_SphereIB = RHIBuffer::Create({ static_cast<uint32_t>(indices.size() * sizeof(uint32_t)), BufferUsage::Index, false, indices.data() });
		}

		// 2. Unit Cone (apex at origin, base at z = 1 with radius 1, closed cap, outward CCW)
		{
			constexpr uint32_t slices = 24;
			std::vector<float> verts;
			std::vector<uint32_t> indices;
			// Vertex 0: apex (0,0,0), Vertex 1: base center (0,0,1)
			verts.insert(verts.end(), { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f });
			for (uint32_t i = 0; i < slices; ++i)
			{
				const float a = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(slices);
				verts.push_back(std::cos(a));
				verts.push_back(std::sin(a));
				verts.push_back(1.0f);
			}
			for (uint32_t i = 0; i < slices; ++i)
			{
				const uint32_t cur = 2u + i;
				const uint32_t next = 2u + ((i + 1u) % slices);
				// Side wall (outward normal points away from +Z axis)
				indices.push_back(0u); indices.push_back(next); indices.push_back(cur);
				// Base cap (outward normal points along +Z)
				indices.push_back(1u); indices.push_back(cur); indices.push_back(next);
			}
			m_ConeIndexCount = static_cast<uint32_t>(indices.size());
			m_ConeVB = RHIBuffer::Create({ static_cast<uint32_t>(verts.size() * sizeof(float)), BufferUsage::Vertex, false, verts.data() });
			m_ConeIB = RHIBuffer::Create({ static_cast<uint32_t>(indices.size() * sizeof(uint32_t)), BufferUsage::Index, false, indices.data() });
		}

		// 3. Fullscreen NDC quad (vec3 positions) for far-plane fallback
		{
			float quadVerts[] = {
				-1.0f,  1.0f, 0.0f,
				-1.0f, -1.0f, 0.0f,
				 1.0f, -1.0f, 0.0f,
				 1.0f,  1.0f, 0.0f
			};
			uint32_t quadIndices[] = { 0, 1, 2, 2, 3, 0 };
			m_QuadVB = RHIBuffer::Create({ static_cast<uint32_t>(sizeof(quadVerts)), BufferUsage::Vertex, false, quadVerts });
			m_QuadIB = RHIBuffer::Create({ static_cast<uint32_t>(sizeof(quadIndices)), BufferUsage::Index, false, quadIndices });
		}
	}

	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;

	Ref<RHIBuffer> m_LightingUBO;
	Ref<RHIBuffer> m_LocalLightsData;
	uint32_t m_LightCapacity = 128;

	Ref<RHITexture2D> m_DefaultTex;
	Ref<RHIDescriptorSet> m_DescriptorSet;

	Ref<RHIShader> m_StencilShader;
	Ref<RHIShader> m_LocalSimpleShader;
	Ref<RHIShader> m_LocalComplexShader;

	Ref<RHIPipeline> m_VolumeStencilPipeline;
	Ref<RHIPipeline> m_LocalSimplePipeline;
	Ref<RHIPipeline> m_LocalComplexPipeline;
	Ref<RHIPipeline> m_FallbackSimplePipeline;
	Ref<RHIPipeline> m_FallbackComplexPipeline;

	Ref<RHIBuffer> m_SphereVB, m_SphereIB;
	uint32_t m_SphereIndexCount = 0;
	Ref<RHIBuffer> m_ConeVB, m_ConeIB;
	uint32_t m_ConeIndexCount = 0;
	Ref<RHIBuffer> m_QuadVB, m_QuadIB;

	RGTextureHandle m_Normal, m_Albedo, m_MetallicRoughness, m_Emissive, m_Velocity;
	RGTextureHandle m_Depth, m_DepthCopy, m_Light;

	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;
};

// Keep the GBuffer inputs intact for lighting, then build the opaque scene
// attachments used by transparency, TAA and post processing.
class ForwardOpaquePass : public RenderGraphPass
{
public:
	ForwardOpaquePass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(std::move(scene)), m_FrameData(std::move(frameData)) {}

	const char* GetName() const override { return "ForwardOpaque"; }

	void SetViewportSize(uint32_t width, uint32_t height) override
	{
		m_Width = width;
		m_Height = height;
	}

	void Setup(RenderGraphBuilder& builder) override
	{
		m_Inputs = { builder.ReadTexture("DeferredLight.Light", RGAccess::CopySrc),
			builder.ReadTexture("Gbuffer.Velocity", RGAccess::CopySrc),
			builder.ReadTexture("Gbuffer.Depth", RGAccess::CopySrc) };
		m_Shadow = builder.ReadTexture("ShadowMap.Depth");

		RDGTextureDesc desc;
		desc.width = m_Width;
		desc.height = m_Height;
		desc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled |
			TextureUsage::TransferSrc | TextureUsage::TransferDst;
		desc.format = Format::RGBA16F;
		m_Outputs[0] = builder.CreateTexture("SceneColor", desc);
		desc.format = Format::RG16F;
		m_Outputs[1] = builder.CreateTexture("Velocity", desc);
		desc.format = Format::D24_UNORM_S8_UINT;
		desc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled |
			TextureUsage::TransferSrc | TextureUsage::TransferDst;
		desc.minFilter = FilterMode::Nearest;
		desc.magFilter = FilterMode::Nearest;
		m_Outputs[2] = builder.CreateTexture("Depth", desc);

		for (size_t i = 0; i < m_Inputs.size(); ++i)
			builder.DescribeTextureCopy(m_Inputs[i], m_Outputs[i]);
		for (auto output : m_Outputs)
		{
			builder.WriteTexture(output, RGAccess::CopyDst);
			builder.Export(output);
		}
		builder.SetColorOutput(0, m_Outputs[0], RGLoadOp::Load);
		builder.SetColorOutput(1, m_Outputs[1], RGLoadOp::Load);
		builder.SetDepthOutput(m_Outputs[2], RGLoadOp::Load);

		PipelineDesc pipe;
		pipe.cullMode = CullMode::Back;
		pipe.depthTest = true;
		pipe.depthWrite = true;
		pipe.pushConstantSize = sizeof(DrawPushConstants);
		pipe.renderingSignature.colorFormats = { Format::RGBA16F, Format::RG16F };
		pipe.renderingSignature.depthFormat = Format::D24_UNORM_S8_UINT;
		m_Renderer.ConfigurePipeline(std::move(pipe));
	}

	void BeforeRender(RenderGraphContext& context) override
	{
		for (size_t i = 0; i < m_Inputs.size(); ++i)
			context.GetCmd().CopyTexture(context.GetTexture(m_Inputs[i]), context.GetTexture(m_Outputs[i]));
	}

	void Execute(RenderGraphContext& context) override
	{
		const ForwardMeshRenderer::Textures textures{
			m_Shadow.id != InvalidResourceId ? context.GetTexture(m_Shadow) : nullptr };
		m_Renderer.UploadFrame(*m_Scene, *m_FrameData, textures.shadow);
		auto& cmd = context.GetCmd();
		cmd.SetDepthTest(true);
		cmd.SetDepthFunc(CompareOp::Less);
		ForwardMeshRenderer::VisitReadyMeshes(*m_Scene, *m_FrameData,
			[&](const Ref<RHIMesh>& mesh, const Ref<Engine::Material>& material,
				const glm::mat4& model, const glm::mat4& previous)
			{
				const auto& reflection = material->GetShader()->GetReflection();
				if (!reflection.Transparent && reflection.RenderPass == MaterialRenderPass::Forward)
					m_Renderer.Draw(cmd, *mesh, material, model, previous, textures);
			});
	}

private:
	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	ForwardMeshRenderer m_Renderer;
	std::array<RGTextureHandle, 3> m_Inputs{}, m_Outputs{};
	RGTextureHandle m_Shadow;
	uint32_t m_Width = 0, m_Height = 0;
};
