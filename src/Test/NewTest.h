#pragma once
#include"Test.h"
#include"Scene/Entity.h"
#include"Scene/Component.h"
#include"Scene/Scene.h"
#include"Scene/Mesh.h"
#include"Panels/Panel.h"
using namespace Component;
using namespace test;
class NewTest :public Test
{
public:

	NewTest(bool EnableFramebuffer = true) :enable_fb(EnableFramebuffer)
	{
		scene = CreateRef<Scene>();
		sr.SetContext(scene);
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

		/*
		scene->m_Registry.each([&](auto entityID)
			{
				Entity entity{ scene.get(),entityID };
				if (entity.HasComponent<Component::Camera>())
				{
					Transform transform = entity.GetComponent<Transform>();
					vec3 rotation = transform.Rotation;
					vec3 front;
					front.x = cos(radians(rotation.y)) * cos(radians(rotation.x));
					front.y = sin(radians(rotation.x));
					front.z = sin(radians(rotation.y)) * cos(radians(rotation.x));

					vec3 cameraFront = normalize(front);
					vec3 Right = normalize(cross(cameraFront, vec3(0.0f, 1.0f, 0.0f)));
					vec3 cameraUp = normalize(cross(Right, cameraFront));

					SceneCamera* camera_s;
					if (cameras.find(entityID)!=cameras.end())
					{
						camera_s = cameras[entityID];
						camera_s->SetPos(entity.GetComponent<Transform>().Position);
					}
					else
					{
						Camera camera = entity.GetComponent<Component::Camera>();
						camera_s = new SceneCamera(entity.GetComponent<Transform>().Position, camera.Target, cameraUp, cameraFront);
						cameras[entityID] = camera_s;
					}
					
					SceneCamera* camera_s = new SceneCamera(entity.GetComponent<Transform>().Position, camera.Target, cameraUp, cameraFront);
					SceneCamera* swap = currentcamera;
					//currentcamera = camera;
					//sr.OnRender();
					//currentcamera = swap;
				}
			});
			*/

	}
	void OnImGuiRender()override {

		//ShowDockSpace();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0,0 });

		ImGui::Begin("ViewPort");
		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		if (m_ViewPortSize != *((vec2*)&viewportPanelSize))
		{
			m_ViewPortSize = { viewportPanelSize.x,viewportPanelSize.y };
			glViewport(0, 0, m_ViewPortSize.x, m_ViewPortSize.y);
			fb->Rsetsize(m_ViewPortSize);
		}
		//ImGui::Image((void*)fb->GetClolorAttachmentRenderID(), ImVec2{ m_ViewPortSize.x,m_ViewPortSize.y }, ImVec2{ 0,1 }, ImVec2{ 1,0 });
		ImGui::Image((void*)fb->GetClolorAttachmentRenderID(), ImVec2{ m_ViewPortSize.x,m_ViewPortSize.y }, ImVec2{ 0,1 }, ImVec2{ 1,0 });
		ImGui::End();
		ImGui::PopStyleVar();
	}
	
	Ref<FrameBuffer>& getfb() override { return fb; }
private:
	Ref<Scene> scene;
	bool enable_fb = true;
	Ref<FrameBuffer> fb;
	vec2 m_ViewPortSize;
	unordered_map<entity, SceneCamera*>cameras;
};