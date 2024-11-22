#include"Debug.h"
#include <spdlog/sinks/stdout_color_sinks.h>
namespace Engine {

	Ref<spdlog::logger> Log::s_CoreLogger;
	Ref<spdlog::logger> Log::s_ClientLogger;

	void Log::Init()
	{
		//auto colorsink = createref<spdlog::sinks::ansicolor_sink>();
		//auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		//sink->set_color(spdlog::level::trace,);
		s_CoreLogger = CreateRef<spdlog::logger>("TSUNDERE");
		s_ClientLogger = CreateRef<spdlog::logger>("APP");

		s_CoreLogger = spdlog::stderr_color_mt("TSUNDERE");
		s_CoreLogger->set_pattern("[%T] %^[%n]: %v%$");
		s_CoreLogger->set_level(spdlog::level::trace);

		s_ClientLogger = spdlog::stderr_color_mt("APP");
		s_ClientLogger->set_pattern("%^[%T] [%n]: %v%$");
		s_ClientLogger->set_level(spdlog::level::trace);
	}
}