#include<Pipeline/Passes/PassCommon.h>
#include<Pipeline/RenderPassRegistry.h>

class ShadowMapPass :public RenderGraphPass {
public:
	ShadowMapPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData) {





	}


	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;


	RGTextureHandle m_Depth;
	Ref<RHIBuffer> m_DrawUBO;
	Ref<RHIBuffer> m_FrameUBO;
	Ref<RHIDescriptorSet> m_ShdowPassDescriptSet;


	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;

	int m_FrameCount = 0;
	glm::mat4 m_PrevViewProjMatrix = glm::mat4(1.0f);
};