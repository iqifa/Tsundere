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

	Ptr<FrameBuffer> m_VelocityFrameBuffer;
	Ptr<FrameBuffer> m_TaaFrameBuffers[2];

	Ptr<FrameBuffer> m_PrevDepthFrameBuffer;


	vec2 m_ViewPortSize;
	FrameBufferSpecification m_BaseFboSpec;
	FrameBufferSpecification m_MsaaFboSpec;

	bool open_Msaa = false;
	bool rendertow = false;
	mat4 proj, view,model;
																																																																																																																																																																																																																																																																																																																																																																																																																																																																																																																																																										
	std::deque<Entity>destory;

	GLFWwindow* m_WindowHandle = nullptr;


	Ref<GeometryPass> geometrypass;
	RenderResources renderResources;
private:
	bool m_ViewportFocused = false;



	bool open_TAA = false;
	// --- TAA ���״̬����� ---
	int m_CurrentFrameIndex = 0;
	int m_FrameCount = 0;
	mat4 m_PrevViewProjMatrix = mat4(1.0f);
	mat4 m_UnjitteredProjMatrix = mat4(1.0f);

	// --- ȫ���ı��� (���� TAA ����) ---
	Ptr<VertexArray> m_QuadVA;
	Ptr<VertexBuffer> m_QuadVB;
	Ptr<IndexBuffer> m_QuadIB;

	Ptr<Shader> m_TaaShader;

	vec2 GetHaltonJitter(int index);
	mat4 Jittering(const mat4& originalProj, float width, float height);
};