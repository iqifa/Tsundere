
#include"glm/glm.hpp"
#include<iostream>
#include"Core/Core.h"

#include"HeadLine.h"

#include"spdlog/spdlog.h"
#include"spdlog/sinks/stdout_color_sinks.h"

using namespace glm;
#ifndef DEBUG

#define DEBUG
#define debuglog(...)		::Engine::Log::GetCoreLogger()->trace(__VA_ARGS__);
#define debugerror(...)		::Engine::Log::GetCoreLogger()->error(__VA_ARGS__);
#define debugwarring(...)	::Engine::Log::GetCoreLogger()->warn(__VA_ARGS__);



namespace Engine {
	class T_API Log
	{
	public:
		static void Init();

		static Ref<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static Ref<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
	private:
		static Ref<spdlog::logger> s_CoreLogger;
		static Ref<spdlog::logger> s_ClientLogger;
	};
}
#endif // !DUBUG

