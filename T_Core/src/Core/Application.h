#pragma once
#include"Core.h"
#include"Window.h"
#include"HeadLine.h"
namespace Engine {
	class T_API Application
	{
	public:
		Application();
		virtual ~Application();


		void Run();
		void OnEvents(Eventing::Event<> &ev);
	private:
		Ptr<Window> m_Window;
		bool m_Running = true;
	};

	Application* CreateApplication();
}
