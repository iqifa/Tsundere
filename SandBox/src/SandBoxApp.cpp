#include<Tsunder.h>
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
#include"ExampleLayer.h"
#include"Inspect.h"
#include"Panels/ModelImportPanel.h"
#include"Panels/TextureImportPanel.h"
#include"HeadLine.h"
class SandBox :public Engine::Application
{
public:
	SandBox() {
		Ref <Scene>m_ActivateScene = CreateRef<Scene>();
		PushLayer(new ExampleLayer(m_ActivateScene,"Example"));
		PushLayer(new Engine::ImGuiLayer("IMGUI"));
		PushLayer(new Inspect(m_ActivateScene, "Inspect"));
		PushLayer(new ModelImportPanel("Model Import"));
		PushLayer(new TextureImportPanel("Texture Import"));
	}
	~SandBox() {}

};
Engine::Application* Engine::CreateApplication()
{
	std::cout << "Create Sandbox" << std::endl;
	return new SandBox();
}
