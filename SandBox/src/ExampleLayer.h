#pragma once
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
class ExampleLayer :public Engine::Layer {
public:
	ExampleLayer(string name = "Example");
	
	void OnUpdate() override;

	void OnEvent(Eventing::Event<>& event) override;
};