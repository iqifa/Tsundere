#pragma once
#include "Event/Event.h"
#include "Panels/ImGuiLayer.h"
#include "Scene/Scene.h"
#include "Panels/Panel.h"
#include "Panels/ComponentInspectors.h"

class Inspect : public BasePanel
{
public:
	Inspect(Ref<Scene> scene, std::string name = "Example");

	Ref<Scene> m_Context;

	void OnUpdate() override;
	void OnImGuiRender() override;
	void OnEvent(Eventing::Event<>& event) override;
};
