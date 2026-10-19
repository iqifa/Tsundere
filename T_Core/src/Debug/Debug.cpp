#include"Debug.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/base_sink.h>

#include <deque>
#include <mutex>

namespace {

	std::mutex g_ConsoleMutex;
	std::deque<Engine::ConsoleLine> g_ClientLines;
	constexpr size_t kMaxClientLines = 1000;

	void AppendClientLine(int level, std::string text)
	{
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
			text.pop_back();

		std::lock_guard<std::mutex> lock(g_ConsoleMutex);
		g_ClientLines.push_back({ level, std::move(text) });
		while (g_ClientLines.size() > kMaxClientLines)
			g_ClientLines.pop_front();
	}

	class ClientConsoleSink : public spdlog::sinks::base_sink<std::mutex>
	{
	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override
		{
			spdlog::memory_buf_t formatted;
			formatter_->format(msg, formatted);
			AppendClientLine(static_cast<int>(msg.level),
				std::string(formatted.data(), formatted.size()));
		}

		void flush_() override {}
	};

}

namespace Engine {

	Ref<spdlog::logger> Log::s_CoreLogger;
	Ref<spdlog::logger> Log::s_ClientLogger;

	void Log::Init()
	{
		s_CoreLogger = spdlog::stderr_color_mt("TSUNDERE");
		s_CoreLogger->set_pattern("[%T] %^[%n]: %v%$");
		s_CoreLogger->set_level(spdlog::level::trace);

		auto consoleSink = std::make_shared<ClientConsoleSink>();
		s_ClientLogger = std::make_shared<spdlog::logger>("APP", consoleSink);
		s_ClientLogger->set_pattern("[%T] [%l] %v");
		s_ClientLogger->set_level(spdlog::level::trace);
	}

	void Log::CopyClientConsole(std::vector<ConsoleLine>& out)
	{
		std::lock_guard<std::mutex> lock(g_ConsoleMutex);
		out.assign(g_ClientLines.begin(), g_ClientLines.end());
	}

	void Log::ClearClientConsole()
	{
		std::lock_guard<std::mutex> lock(g_ConsoleMutex);
		g_ClientLines.clear();
	}
}