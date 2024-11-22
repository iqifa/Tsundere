#include<Tsunder.h>
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
#include"ExampleLayer.h"
class SandBox:public Engine::Application
{
public:
	SandBox(){
		PushLayer(new ExampleLayer("Example"));
		PushLayer(new Engine::ImGuiLayer());
	}
	~SandBox(){}

private:

};
Engine::Application* Engine::CreateApplication()
{
	std::cout << "Create Sandbox" << endl;
	return new SandBox();
}
