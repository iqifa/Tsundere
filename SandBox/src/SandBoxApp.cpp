#include<Tsunder.h>
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
class ExampleLayer :public Engine::Layer {
public:
	ExampleLayer()
		: Layer("Example")
	{
	}

	void OnUpdate() override
	{
		Info_Core("ExampleLayer::Update");
	}

	void OnEvent(Eventing::Event<>& event) override
	{
		event.Invoke();
	}

};
class SandBox:public Engine::Application
{
public:
	SandBox(){
		PushLayer(new ExampleLayer());
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
