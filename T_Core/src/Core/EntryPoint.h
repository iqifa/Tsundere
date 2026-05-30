#pragma once
#include"Application.h"
#include"Debug/Debug.h"
#ifdef T_PLATFORM_WINDOWS

extern Engine::Application* Engine::CreateApplication();


int main(int argc,char** argv)
{
	Engine::Log::Init();

	auto app = Engine::CreateApplication();
	Info_Core("Start Tsundere!")
	app->Run();



	delete(app);
}

#endif