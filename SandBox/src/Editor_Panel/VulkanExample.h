#pragma once
#include<Panels/Panel.h>
#include<Scene/Scene.h>
#include<Pipeline/RenderPass.h>
#include<Pipeline/RenderGraph.h>
#include<Pipeline/RenderPassManager.h>

class VulkanExampleLayer :public BasePanel {
public:
	VulkanExampleLayer(Ref<Scene>scene, std::string name = "Example");

private:
	Ref<Scene>m_Context;


	void OnUpdate() override;
	void OnImGuiRender()override;
	void OnEvent(Eventing::Event<>& event) override;


	void ExecuteRenderGraph();
	void BuildRenderGraph(uint32_t width, uint32_t height);

	vec2 m_ViewPortSize{ 0.0f };
	GLFWwindow* m_WindowHandle = nullptr;


	uint32_t m_GraphBuiltWidth = 0;
	uint32_t m_GraphBuiltHeight = 0;
	bool m_GraphDirty = true;


	Ref<RGFrameData> m_GraphFrameData;
	RGTextureHandle m_GraphFinalColor;
	RGTextureHandle m_GraphVelocity;

	RHITexture2D* m_FinalColorTexture = nullptr;
	ImTextureID m_FinalColorImGuiID = ImTextureID_Invalid;


	RenderGraph m_RenderGraph;
	RenderPassManager m_PassManager;  // Pass 管理器


	bool m_ViewportFocused = false;
	bool m_ShowPassManager = true;  // 是否显示 Pass 管理面板

};