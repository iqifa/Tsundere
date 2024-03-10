#include"Debug.h"
void log(const std::string str, std::string name, const char* file, int line)
{
	std::cout << name << str;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}
void log(vec3& vec, const std::string name, const char* file, int line)
{
	std::cout << name << "x:" << vec[0] << "\t";
	std::cout << name << "y:" << vec[1] << "\t";
	std::cout << name << "z:" << vec[2] << "\t";
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}
void log(unsigned int unit, const std::string name, const char* file, int line)
{
	std::cout << name << ":" << unit;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}
void log(const std::string str, const char* file, int line)
{
	std::cout << str;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}

void log(float num, const char* file, int line)
{
	std::cout << num;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}

void log(mat4 mat, std::string name, const char* file, int line)
{
	std::cout << " " << mat[0].x << "\t" << mat[0].y << "\t" << mat[0].z << "\t" << mat[0].w << std::endl;
	std::cout << " " << mat[1].x << "\t" << mat[1].y << "\t" << mat[1].z << "\t" << mat[1].w << std::endl;
	std::cout << " " << mat[2].x << "\t" << mat[2].y << "\t" << mat[2].z << "\t" << mat[2].w << std::endl;
	std::cout << " " << mat[3].x << "\t" << mat[3].y << "\t" << mat[3].z << "\t" << mat[3].w << std::endl;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}

void error(std::string msg, const char* file, int line)
{
	std::cout << "\033[1;31mError:";
	std::cout << msg;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
	std::cout << "\033[0m";
	__debugbreak();
}
void warring(const std::string str, const char* file, int line)
{
	std::cout << "\033[1;33mWarring:" << str << "\tFile:" << file << "\tLine:" << line << "\033[0m" << std::endl;
}
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