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

	TestScene(bool EnableFramebuffer = false) :enable_fb(EnableFramebuffer)
	{
		sence = CreateRef<Scene>();
		panel.SetContext(sence);
		panel.SetHeadTitle("Hierarchy");

		Entity entity = sence->CreateEntity("Entity");
		sr.SetContext(sence);
		entity.AddComponent<MeshFile>();
		entity.AddComponent<Material>("res/shaders/default.shader");
		tex = CreateRef<Texture>("res/texture/jinx.jpeg");
		if (EnableFramebuffer)
			fb = CreateRef<FrameBuffer>();
	}
	void OnRender()override
	{
		RenderCall();
	}
	void  RenderCall() override {
		Renderer renderer;
		fb->Bind();
		renderer.Clear();
		if (currentcamera)
			GLCall(currentcamera->RenderSkyBox());
		sr.OnRender();
		fb->UnBind();
	}
	void OnImGuiRender()override {
		panel.OnImGUIRender();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0,0 });

		ImGui::Begin("ViewPort");
		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		if (m_ViewPortSize != *((vec2*)&viewportPanelSize))
		{
			m_ViewPortSize = { viewportPanelSize.x,viewportPanelSize.y };
			glViewport(0, 0, m_ViewPortSize.x, m_ViewPortSize.y);
			fb->Rsetsize(m_ViewPortSize);
		}
		ImGui::Image((void*)fb->GetClolorAttachmentRenderID(), ImVec2{ m_ViewPortSize.x,m_ViewPortSize.y }, ImVec2{ 0,1 }, ImVec2{ 1,0 });
		ImGui::End();
		ImGui::PopStyleVar();
	}
	Ref<FrameBuffer>& getfb() override  { return fb; }
private:
	Panel panel;
	Panel viewport;
	Ref<Scene> sence;
	bool enable_fb = true;
	Ref<FrameBuffer> fb;
	Ref<Texture> tex;
	vec2 m_ViewPortSize;
};