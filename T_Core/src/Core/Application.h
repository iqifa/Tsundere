#pragma once
#include"Core.h"
#include"Window.h"
#include"Layer/LayerStack.h"
#include"HeadLine.h"
namespace Engine {
	class T_API Application
	{
	public:
		Application();
		virtual ~Application();


		void Run();
		void OnEvents(Eventing::Event<> &ev);
		void PushLayer(Layer* layer);
		void PopLayer(Layer* layer);
	private:
		Ptr<Window> m_Window;
		bool m_Running = true;

		LayerStack layerStack;
	};

	Application* CreateApplication();
}
