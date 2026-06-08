#pragma once
#include"Core.h"
#include"Window.h"
#include"Layer/LayerStack.h"
#include"HeadLine.h"
#include"ExternalFiles.h"
#include"Panels/ImGuiLayer.h"

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
		Window& GetWindow() { return *m_Window; }

		static Application& Get() { return *Instance; }
	private:
		static Application* Instance;

		Ptr<Window> m_Window;
		bool m_Running = true;
		LayerStack layerStack;
		ImGuiLayer *m_iml;
		};

	Application* CreateApplication();
}
