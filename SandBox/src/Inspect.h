#pragma once
#include "Event/Event.h"
#include "Panels/ImGuiLayer.h"
#include "Scene/Scene.h"
#include "Panels/Panel.h"
#include "Panels/ComponentInspectors.h"
#include "Panels/AssetsPanel.h"

class Inspect : public BasePanel
{
public:
	Inspect(Ref<Scene> scene, std::string name = "Example");

	Ref<Scene> m_Context;
	std::string m_SceneFile;
	std::string m_SceneFileError;

	AssetsPanel* m_Assets = nullptr;
	uint64_t m_SceneAssetUid = 0;
	void LoadScene(const std::string& path);
	void SaveScene(bool saveAs = false);
	void RememberSceneAsset();

	void OnUpdate() override;
	void OnImGuiRender() override;
	void OnEvent(Eventing::Event<>& event) override;

	void DrawEntityNode(Entity entity);

};
