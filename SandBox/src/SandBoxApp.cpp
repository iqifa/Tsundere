#include<Tsunder.h>
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
#include"ExampleLayer.h"
#include"Inspect.h"
#include"HeadLine.h"
class SandBox :public Engine::Application
{
public:
	SandBox() {
		Ref <Scene>m_ActivateScene = CreateRef<Scene>();
		PushLayer(new ExampleLayer(m_ActivateScene,"Example"));
		PushLayer(new Engine::ImGuiLayer());
		PushLayer(new Inspect(m_ActivateScene, "Inspect"));
	}
	~SandBox() {}

};
Engine::Application* Engine::CreateApplication()
{
	std::cout << "Create Sandbox" << std::endl;
	return new SandBox();
}
