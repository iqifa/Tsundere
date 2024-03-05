#pragma once
#include"Core.h"

namespace Engine {
	class T_API Application
	{
	public:
		Application();
		virtual ~Application();


		void Run();
	private:

	};

	Application* CreateApplication();
}
