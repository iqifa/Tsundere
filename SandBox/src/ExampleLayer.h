#pragma once
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
#include"Scene/Scene.h"
#include"Panels/Panel.h"
#include"GLHead.h"

class ExampleLayer :public BasePanel {
public:
public:
	ExampleLayer(Ref<Scene>scene, std::string name = "Example");
private:
	Ref<Scene>m_Context;

	
	
	void OnUpdate() override;
	void OnImGuiRender()override;

	void OnEvent(Eventing::Event<>& event) override;
	void DrawEntityNode(Entity entity);

	Ptr<VertexArray>va;
	Ptr<VertexBuffer>vb;
	Ptr<IndexBuffer>ibo;
	Ptr<Shader>shader;

	Ptr<FrameBuffer> framebuffer;
	Ptr<FrameBuffer> Msaaframebuffer;
	vec2 m_ViewPortSize;

	bool open_Msaa = false;
	mat4 proj, view,model;
																																																																																																																																																																																																																																																																																																																																																																																																																																																																																																																																																										
	std::deque<Entity>destory;
};