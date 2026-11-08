#pragma once
#include "Pipeline/Passes/PassCommon.h"

class TAAPass : public RenderGraphPass
{
private:
	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	Ref<RHIShader>m_TAAShader;
	Ref<RHIBuffer>PerPass_TAA;
	Ref<RHIDescriptorSet> m_TAAPassDescriptSet;
	Ref<RHITexture2D> m_DefaultTex;


	RGTextureHandle m_SceneColor;
	RGTextureHandle m_HistoryColor;
	RGTextureHandle m_Velocity;
	RGTextureHandle m_Depth;
	RGTextureHandle m_HistoryDepth;

RGTextureHandle m_OutputColor;
RGTextureHandle m_OutputDepth;


RHITexture2D* m_HistoryColorTex;
RHITexture2D* m_HistoryDepthTex;
	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;

	struct PerPass_TAA_Parameter
	{
		glm::mat4 Inv_vp;
		glm::mat4 Pre_vp;/*  */
	};


public:
	TAAPass(Ref<Scene>scene, Ref< RGFrameData>frameData) : m_Scene(scene), m_FrameData(frameData)
	{
		m_TAAShader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/TAA.shader");
		PerPass_TAA = RHIBuffer::Create(BufferDesc{ sizeof(PerPass_TAA_Parameter),BufferUsage::Uniform,true,nullptr});
	
		m_TAAPassDescriptSet = RHIDescriptorSet::Create();
		

		m_DefaultTex = RHITexture2D::Create({ 1, 1, Format::RGBA8_UNORM, FilterMode::Linear, FilterMode::Linear,
											 WrapMode::ClampToEdge, WrapMode::ClampToEdge, false, nullptr });

		if (m_TAAPassDescriptSet)
		{
			m_TAAPassDescriptSet->BindUniformBuffer(0, PerPass_TAA, sizeof(PerPass_TAA_Parameter));
			m_TAAPassDescriptSet->BindTexture(10, m_DefaultTex,10);
			m_TAAPassDescriptSet->BindTexture(11, m_DefaultTex,11);
			m_TAAPassDescriptSet->BindTexture(12, m_DefaultTex,12);
			m_TAAPassDescriptSet->BindTexture(13, m_DefaultTex,13);
			m_TAAPassDescriptSet->BindTexture(14, m_DefaultTex,14);
		}
		else {
			Error_Core("[{0} Pass]:m_TAAPassDescriptSet is Null", GetName());
		}
	}
	const char* GetName() const override { return "TAA"; }


	void Setup(RenderGraphBuilder& builder) override
	{
		RDGTextureDesc colorDesc;
		colorDesc.width = m_Width;
		colorDesc.height = m_Height;
		colorDesc.format = Format::RGBA8_UNORM;
		colorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

		RDGTextureDesc depthDesc;
		depthDesc.width = m_Width;
		depthDesc.height = m_Height;
		depthDesc.format = Format::D24_UNORM_S8_UINT;
		depthDesc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled;

		m_SceneColor = builder.ReadTexture("DeferredLight.Light", RGAccess::ReadSRV);
		m_Depth=builder.ReadTexture("Gbuffer.Depth", RGAccess::DepthRead);
		m_Velocity = builder.ReadTexture("Gbuffer.Velocity", RGAccess::ReadSRV);

		


		m_HistoryColor=builder.ImportTexture(m_HistoryColorTex, colorDesc, "HistoryColor");
		m_HistoryDepth=builder.ImportTexture(m_HistoryDepthTex, depthDesc, "HistoryDepth");


		builder.SetColorOutput(0, m_OutputColor, RGLoadOp::Clear, { 0,0,0,1 });
		// builder.SetDepthOutput(m_OutputDepth,RGLoadOp::Clear,1.0f);


		builder.Export(m_OutputColor);
		// builder.Export(m_OutputDepth);
	}
};

