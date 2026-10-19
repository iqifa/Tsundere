#include<Tsunder.h>
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
//#include"ExampleLayer.h"
#include"Inspect.h"
#include"Editor_Panel/VulkanExample.h"
#include"Panels/ModelImportPanel.h"
#include"Panels/ConsolePanel.h"
#include"Panels/ModelEditorPanel.h"
//#include"Panels/TextureImportPanel.h"
#include"HeadLine.h"
entt::entity m_SelectedContext = null;

class SandBox :public Engine::Application
{
public:
	SandBox() {
		Ref <Scene>m_ActivateScene = CreateRef<Scene>();
		PushLayer(new Engine::ImGuiLayer("IMGUI"));
		#ifdef RenderAPI_Vulkan
			PushLayer(new VulkanExampleLayer(m_ActivateScene,"VulkanExample"));
#else
			PushLayer(new VulkanExampleLayer(m_ActivateScene, "OpenGL- Example"));
#endif
		//PushLayer(new ExampleLayer(m_ActivateScene, "Example"));
		PushLayer(new Inspect(m_ActivateScene, "Inspect"));
		ModelEditorPanel::BindScene(m_ActivateScene);
		PushLayer(new ModelImportPanel("Model Import"));
		PushLayer(new ConsolePanel("Console"));
		//PushLayer(new TextureImportPanel("Texture Import"));
	}
	~SandBox() {}

};
Engine::Application* Engine::CreateApplication()
{
	Info_Client("Create Sandbox");
	return new SandBox();
}
