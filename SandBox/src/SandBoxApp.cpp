#include<Tsunder.h>
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
//#include"ExampleLayer.h"
#include"Inspect.h"
#include"Editor_Panel/VulkanExample.h"
#include"Panels/ConsolePanel.h"
#include"Panels/ModelEditorPanel.h"
#include"Panels/AssetsPanel.h"
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
		auto* inspector = new Inspect(m_ActivateScene, "Inspect");
		PushLayer(inspector);
		ModelEditorPanel::BindScene(m_ActivateScene);
		auto* assets = new AssetsPanel("Assets");
		inspector->m_Assets = assets;
		assets->OnOpenScene = [inspector](const std::string& path) { inspector->LoadScene(path); };
		PushLayer(assets);
		PushLayer(new ConsolePanel("Console"));
	}
	~SandBox() {}

};
Engine::Application* Engine::CreateApplication()
{
	Info_Client("Create Sandbox");
	return new SandBox();
}
