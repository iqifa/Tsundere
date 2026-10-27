#pragma once
#include "Core/Layer/Layer.h"
#include "Panels/EditorWindows.h"
#include "ExternalFiles.h"
#include "Debug/Debug.h"

#include <vector>

class ConsolePanel : public Engine::Layer
{
public:
	explicit ConsolePanel(const std::string& name = "Console")
		: Layer(name) {}

	void OnImGuiRender() override
	{
		if (EditorWindows::Console())
			Draw();
	}

private:
	static ImVec4 ColorFor(int level)
	{
		if (level >= static_cast<int>(spdlog::level::err))
			return ImVec4(1.00f, 0.40f, 0.40f, 1.0f);
		if (level == static_cast<int>(spdlog::level::warn))
			return ImVec4(1.00f, 0.78f, 0.25f, 1.0f);
		return ImVec4(0.55f, 0.86f, 1.00f, 1.0f);
	}

	void Draw()
	{
		if (!ImGui::Begin("Console", &EditorWindows::Console()))
		{
			ImGui::End();
			return;
		}

		if (ImGui::Button("Clear"))
			Engine::Log::ClearClientConsole();
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(static_cast<int>(spdlog::level::info)));
		ImGui::Checkbox("Info", &m_ShowInfo);
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(static_cast<int>(spdlog::level::warn)));
		ImGui::Checkbox("Warn", &m_ShowWarn);
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(static_cast<int>(spdlog::level::err)));
		ImGui::Checkbox("Error", &m_ShowError);
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		ImGui::InputTextWithHint("##consolefilter", "filter", m_Filter, sizeof(m_Filter));

		ImGui::Separator();
		ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

		const float scrollY = ImGui::GetScrollY();
		const float scrollMax = ImGui::GetScrollMaxY();
		const bool wasAtBottom = scrollY >= scrollMax - 1.0f;

		std::vector<Engine::ConsoleLine> lines;
		Engine::Log::CopyClientConsole(lines);
		for (const Engine::ConsoleLine& line : lines)
		{
			const bool isError = line.level >= static_cast<int>(spdlog::level::err);
			const bool isWarn = line.level == static_cast<int>(spdlog::level::warn);
			if (isError && !m_ShowError)
				continue;
			if (isWarn && !m_ShowWarn)
				continue;
			if (!isError && !isWarn && !m_ShowInfo)
				continue;
			if (m_Filter[0] != '\0' && line.text.find(m_Filter) == std::string::npos)
				continue;

			ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(line.level));
			ImGui::TextUnformatted(line.text.c_str());
			ImGui::PopStyleColor();
		}

		if (m_AutoScroll && wasAtBottom)
			ImGui::SetScrollHereY(1.0f);

		ImGui::EndChild();
		ImGui::End();
	}

	bool m_ShowInfo = true;
	bool m_ShowWarn = true;
	bool m_ShowError = true;
	bool m_AutoScroll = true;
	char m_Filter[128] = {};
};
