#pragma once
#include "Pipeline/Passes/OpaqueSceneInputs.h"
#include "Pipeline/Passes/PassCommon.h"

class TAAPass : public RenderGraphPass
{
private:
	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	Ref<RHIShader>m_TAAShader;
	Ref<RHIBuffer>PerPass_TAA;
	Ref<RHIDescriptorSet> m_TAAPassDescriptSet;
	Ref<RHIPipeline>m_TAApipline;
	PipelineDesc m_TAApiplineDesc;
	Ref<RHIBuffer> ScreenVB;
	Ref<RHIBuffer> ScreenIB;
	VertexLayout m_ScreenVertexLayout;

	RGTextureHandle m_SceneColor;
	RGTextureHandle m_HistoryColor;
	RGTextureHandle m_Velocity;
	RGTextureHandle m_Depth;
	RGTextureHandle m_OutputColor;
	Ref<RHITexture2D> m_HistoryColorTex;
	bool m_HistoryValid = false;
	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;

	struct PerPass_TAA_Parameter
	{
		glm::mat4 Inv_vp;
		glm::mat4 Pre_vp;
		glm::vec2 CurrentJitter;
		int32_t HistoryValid;
		int32_t Padding;
	};



public:
	TAAPass(Ref<Scene>scene, Ref< RGFrameData>frameData) : m_Scene(scene), m_FrameData(frameData)
	{
		m_TAAShader = RHIShader::Create("res/shaders/TAA.shader");
		PerPass_TAA = RHIBuffer::Create(BufferDesc{ sizeof(PerPass_TAA_Parameter),BufferUsage::Uniform,true,nullptr });

		m_TAAPassDescriptSet = RHIDescriptorSet::Create();


		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f
		};
		unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };

		ScreenVB = RHIBuffer::Create({ 4 * 4 * (uint32_t)sizeof(float),BufferUsage::Vertex,false,quadVertices });
		ScreenIB = RHIBuffer::Create({ sizeof(quadIndices),BufferUsage::Index,false,quadIndices });


		VertexLayout ScreenVertexLayout;
		ScreenVertexLayout.stride = 4 * sizeof(float);
		ScreenVertexLayout.attributes = {
			{0,VertexFormat::Float2,0,0},
			{1,VertexFormat::Float2,2 * sizeof(float),0},
		};


		PipelineDesc pipeDesc;
		pipeDesc.shader = m_TAAShader;
		pipeDesc.vertexLayout = ScreenVertexLayout;
		pipeDesc.topology = PrimitiveTopology::Triangles;
		pipeDesc.cullMode = CullMode::None;
		pipeDesc.depthTest = false;
		pipeDesc.depthWrite = false;
		m_TAApiplineDesc = pipeDesc;
		m_ScreenVertexLayout = ScreenVertexLayout;
	}
	const char* GetName() const override { return "TAA"; }


	void Setup(RenderGraphBuilder& builder) override
	{
		m_OutputColor = {};
		m_HistoryValid = false;
		if (!m_TAAShader || !m_TAAPassDescriptSet)
			return;

		const auto opaque = ReadOpaqueSceneInputs(builder);
		if (!opaque.IsValid())
			return;

		const auto composite = builder.ReadTexture("Transparent.SceneColor");
		m_SceneColor = composite.id != InvalidResourceId ? composite : opaque.color;
		m_Depth = opaque.depth;
		m_Velocity = opaque.velocity;

		RDGTextureDesc colorDesc;
		colorDesc.width = m_Width;
		colorDesc.height = m_Height;
		colorDesc.format = Format::RGBA8_UNORM;
		colorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferSrc;
		m_OutputColor = builder.CreateTexture("SceneColor", colorDesc);
		builder.SetColorOutput(0, m_OutputColor, RGLoadOp::Clear, { 0,0,0,1 });
		builder.ReadTexture("SceneColor", RGAccess::CopySrc);
		builder.Export(m_OutputColor);

		Texture2DDesc historyDesc;
		historyDesc.width = m_Width;
		historyDesc.height = m_Height;
		historyDesc.format = colorDesc.format;
		historyDesc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
		if (!m_HistoryColorTex || m_HistoryColorTex->GetWidth() != m_Width ||
			m_HistoryColorTex->GetHeight() != m_Height)
			m_HistoryColorTex = RHITexture2D::Create(historyDesc);

		RDGTextureDesc historyResource = colorDesc;
		historyResource.usage = historyDesc.usage;
		builder.ImportTexture(m_HistoryColorTex.get(), historyResource, "HistoryColor");
		m_HistoryColor = builder.ReadTexture("HistoryColor");
		builder.WriteTexture(m_HistoryColor, RGAccess::CopyDst);

		if (!m_TAApipline) {
			m_TAAPassDescriptSet->BindUniformBuffer(0, PerPass_TAA);
			for (uint32_t binding = 10; binding <= 13; ++binding)
				m_TAAPassDescriptSet->BindTexture(binding, m_HistoryColorTex, binding);
			m_TAApiplineDesc.descriptorSets = { m_TAAPassDescriptSet };
			m_TAApipline = builder.CreatePipeline(m_TAAShader, m_ScreenVertexLayout, &m_TAApiplineDesc);
			if (m_TAApipline) {
				m_TAApipline->SetupVertexFormat(ScreenVB);
				m_TAApipline->SetupIndexBuffer(ScreenIB);
			}
		}
	}


	void Execute(RenderGraphContext& context) override {
		if (!m_TAApipline || m_OutputColor.id == InvalidResourceId)
			return;

		auto& cmd = context.GetCmd();
		cmd.SetDepthTest(false);
		cmd.SetDepthFunc(CompareOp::Always);


		const auto& frameCamera = m_FrameData->Camera;


		PerPass_TAA_Parameter pp_taa_param{};
		pp_taa_param.Inv_vp = glm::inverse(frameCamera.JitteredViewProj);
		pp_taa_param.Pre_vp = frameCamera.PrevViewProj;
		pp_taa_param.CurrentJitter = frameCamera.JitterUV;
		pp_taa_param.HistoryValid = m_HistoryValid && frameCamera.HistoryValid;

		PerPass_TAA->Upload(&pp_taa_param, sizeof(PerPass_TAA_Parameter));

		RHITexture2D* sceneColorTex = context.GetTexture(m_SceneColor);
		RHITexture2D* historyColorTex = pp_taa_param.HistoryValid
			? context.GetTexture(m_HistoryColor) : sceneColorTex;
		RHITexture2D* velocityTex = context.GetTexture(m_Velocity);
		RHITexture2D* depthTex = context.GetTexture(m_Depth);
		m_TAAPassDescriptSet->Reset();
		m_TAAPassDescriptSet->BindUniformBuffer(0, PerPass_TAA);
		m_TAAPassDescriptSet->BindTexture(10, sceneColorTex, 10);
		m_TAAPassDescriptSet->BindTexture(11, historyColorTex, 11);
		m_TAAPassDescriptSet->BindTexture(12, velocityTex, 12);
		m_TAAPassDescriptSet->BindTexture(13, depthTex, 13);
		m_TAAPassDescriptSet->Apply(0);

		cmd.BindPipeline(m_TAApipline);
		cmd.BindVertexBuffer(ScreenVB, 0);
		cmd.BindIndexBuffer(ScreenIB);
		cmd.BindDescriptorSet(m_TAAPassDescriptSet, 0);
		cmd.DrawIndexed(6);
		m_TAApipline->Unbind();
	}

	void AfterRender(RenderGraphContext& context) override
	{
		m_HistoryValid = false;
		if (!m_TAApipline)
			return;

		auto* output = context.GetTexture(m_OutputColor);
		auto* history = context.GetTexture(m_HistoryColor);
		if (!output || !history)
			return;

		context.GetCmd().CopyTexture(output, history);
		m_HistoryValid = true;
	}

	void SetViewportSize(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;
	}

};

REGISTER_RENDER_PASS(TAAPass, "TAA", "PostProcessing", 20, true)
