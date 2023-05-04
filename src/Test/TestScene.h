#pragma once
#include"Test.h"
#include"Scene/Entity.h"
#include"Scene/Component.h"
#include"Scene/Scene.h"
#include"Scene/Mesh.h"
#include"Panels/Panel.h"
using namespace Component;
using namespace test;
class TestScene :public Test
{
public:

	TestScene()
	{
		sence = CreateRef<Scene>();
		panel.SetContext(sence);
		panel.SetHeadTitle("Hierarchy");

		sence->CreateEntity("Entity");
		sr.SetContext(sence);
	}

	void  OnRender() override {
		
		sr.OnRender();
	}
	void OnImGuiRender()override {
		panel.OnImGUIRender();
	}
private:
	Panel panel;
	Ref<Scene> sence;
};