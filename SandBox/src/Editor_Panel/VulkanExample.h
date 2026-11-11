#pragma once
#include<Panels/Panel.h>
#include<Scene/Scene.h>
#include<Pipeline/RenderPass.h>
#include<Pipeline/RenderGraph.h>
#include<Pipeline/RenderPassManager.h>
#include <Pipeline/Passes/PostProcessPass.h>

class VulkanExampleLayer :public BasePanel {
public:
	VulkanExampleLayer(Ref<Scene>scene, std::string name = "Example");
	void DrawPostProcessMenu();

private:
	Ref<Scene>m_Context;


	void OnUpdate() override;
	void OnImGuiRender()override;
	void OnEvent(Eventing::Event<>& event) override;


	void ExecuteRenderGraph();
	void BuildRenderGraph(uint32_t width, uint32_t height);
	void DrawSceneViewport();
	void DrawViewportTools(const ImVec2& min, const ImVec2& max, bool hovered);
	void DrawSkyboxSettings();
	void SaveSkyboxSettings();

	enum class ViewportTool { Select, Move, Rotate, Scale };
	ViewportTool m_ViewportTool = ViewportTool::Move;
	struct TransformDrag
	{
		entt::entity entity = entt::null;
		uint64_t sceneRevision = 0;
		int axis = 0;
		Component::Transform start;
		glm::vec2 mouseStart{ 0.0f };
		glm::vec2 direction{ 0.0f };
		float unitsPerPixel = 0.0f;
	};
	TransformDrag m_TransformDrag;

	glm::vec2 m_ViewPortSize{ 0.0f };


	uint32_t m_GraphBuiltWidth = 0;
	uint32_t m_GraphBuiltHeight = 0;
	bool m_GraphDirty = true;
	uint64_t m_SceneRevision = 0;
	uint64_t m_AttemptedSkyboxAsset = 0;
	std::string m_LoadedSkyboxAsset;
	std::string m_SkyboxStatus;
	std::string m_SkyboxConfigPath;
	std::string m_SkyboxSaveStatus;
	bool m_SkyboxChecked = false;
	uint64_t m_SkyboxAssetRevision = 0;
	PostProcess::Settings m_PostProcess;
	Ref<RHIShader> m_PostProcessShader;


	Ref<RGFrameData> m_GraphFrameData;
	RGTextureHandle m_GraphFinalColor;
	RGTextureHandle m_GraphVelocity;
	RGTextureHandle m_ShadowMap;

	RHITexture2D* m_FinalColorTexture = nullptr;
	ImTextureID m_FinalColorImGuiID = ImTextureID_Invalid;
	RHITexture2D* m_ShadowMapTexture = nullptr;
	ImTextureID m_ShadowMapImGuiID = ImTextureID_Invalid;
	

	RenderGraph m_RenderGraph;
	RenderPassManager m_PassManager;  // Pass 管理器
	bool m_GeometryMSAAEnabled = true;
	bool m_TAAOutputAvailable = false;


	bool m_ViewportCameraControl = false;
	bool m_ShowPassManager = true;  // 是否显示 Pass 管理面板

};
