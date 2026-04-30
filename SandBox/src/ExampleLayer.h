#pragma once
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
#include"Scene/Scene.h"
#include"Panels/Panel.h"
#include"GLHead.h"
#include<Pipeline/RenderPass.h>
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
	Ref<Shader>shader;

	Ref<FrameBuffer> framebuffer;
	Ptr<FrameBuffer> Msaaframebuffer;

	vec2 m_ViewPortSize;
	FrameBufferSpecification m_BaseFboSpec;
	FrameBufferSpecification m_MsaaFboSpec;

	bool open_Msaa = false;
	bool rendertow = false;

	std::deque<Entity>destory;

	GLFWwindow* m_WindowHandle = nullptr;

	Ref<GeometryPass> geometrypass;
	Ref<TAAPass> taaPass;
	RenderResources renderResources;

	bool m_ViewportFocused = false;
};