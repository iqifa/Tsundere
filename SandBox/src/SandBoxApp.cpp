#include<Tsunder.h>
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
//#include"ExampleLayer.h"
//#include"Inspect.h"
#include"Editor_Panel/VulkanExample.h"
//#include"Panels/ModelImportPanel.h"
//#include"Panels/TextureImportPanel.h"
#include"HeadLine.h"
class SandBox :public Engine::Application
{
public:
	SandBox() {
		Ref <Scene>m_ActivateScene = CreateRef<Scene>();
		PushLayer(new Engine::ImGuiLayer("IMGUI"));
		#ifdef RenderAPI_Vulkan
			PushLayer(new VulkanExampleLayer(m_ActivateScene,"VulkanExample"));
#else
			PushLayer(new VulkanExampleLayer(m_ActivateScene, "Example"));
#endif
		//PushLayer(new ExampleLayer(m_ActivateScene, "Example"));
		//PushLayer(new Inspect(m_ActivateScene, "Inspect"));
		//PushLayer(new ModelImportPanel("Model Import"));
		//PushLayer(new TextureImportPanel("Texture Import"));
	}
	~SandBox() {}

};
Engine::Application* Engine::CreateApplication()
{
	std::cout << "Create Sandbox" << std::endl;
	return new SandBox();
}
