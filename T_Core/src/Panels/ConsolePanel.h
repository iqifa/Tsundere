#pragma once
#include "Core/Layer/Layer.h"
#include "Panels/EditorWindows.h"
#include "ExternalFiles.h"
#include "Debug/Debug.h"

#include <vector>
#include <algorithm>
#include <cfloat>

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
		{
			Engine::Log::ClearClientConsole();
			m_Anchor = {};
			m_Caret = {};
		}
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

		std::vector<Engine::ConsoleLine> lines;
		Engine::Log::CopyClientConsole(lines);
		struct Entry { uint64_t id; size_t begin, end; };
		struct Row { size_t begin, end; float y, height; int level; };
		std::vector<Entry> entries;
		std::vector<Row> rows;
		std::string text;
		float height = 0.0f, width = 0.0f;
		const float lineHeight = ImGui::GetTextLineHeight();
		for (const auto& line : lines)
		{
			const bool isError = line.level >= static_cast<int>(spdlog::level::err);
			const bool isWarn = line.level == static_cast<int>(spdlog::level::warn);
			if ((isError && !m_ShowError) || (isWarn && !m_ShowWarn) ||
				(!isError && !isWarn && !m_ShowInfo)) continue;
			if (m_Filter[0] && line.text.find(m_Filter) == std::string::npos) continue;
			const size_t begin = text.size();
			text += line.text;
			text += '\n';
			entries.push_back({line.id, begin, text.size()});
			for (size_t rowBegin = begin; rowBegin < text.size(); )
			{
				const size_t rowEnd = text.find('\n', rowBegin);
				const bool last = rowEnd + 1 == text.size();
				const float rowHeight = lineHeight + (last ? ImGui::GetStyle().ItemSpacing.y : 0.0f);
				rows.push_back({rowBegin, rowEnd, height, rowHeight, line.level});
				width = (std::max)(width, TextWidth(text, rowBegin, rowEnd));
				height += rowHeight;
				rowBegin = rowEnd + 1;
			}
		}
		auto offset = [&](TextPosition position) -> size_t {
			for (const auto& entry : entries)
				if (entry.id == position.id)
					return entry.begin + (std::min)(position.byte, entry.end - entry.begin);
			return std::string::npos;
		};
		auto position = [&](size_t byte) -> TextPosition {
			for (const auto& entry : entries)
				if (byte < entry.end) return {entry.id, byte - entry.begin};
			return entries.empty() ? TextPosition{} : TextPosition{entries.back().id, entries.back().end - entries.back().begin};
		};
		if (offset(m_Anchor) == std::string::npos || offset(m_Caret) == std::string::npos)
			m_Anchor = m_Caret = {};

		ImGui::Separator();
		ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
		const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 surfaceSize((std::max)(width + 4.0f, (std::max)(available.x - ImGui::GetScrollX(), 1.0f)),
			(std::max)(height, (std::max)(available.y - ImGui::GetScrollY(), 1.0f)));
		ImGui::InvisibleButton("##logtext", surfaceSize);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
		auto hit = [&](ImVec2 mouse) -> size_t {
			if (rows.empty()) return 0;
			const float y = mouse.y - origin.y;
			if (y < 0.0f) return 0;
			if (y >= height) return text.size();
			const Row* row = &rows.back();
			for (const auto& candidate : rows)
				if (y < candidate.y + candidate.height) { row = &candidate; break; }
			const float x = mouse.x - origin.x;
			float cursor = 0.0f;
			for (size_t byte = row->begin; byte < row->end; )
			{
				size_t next = byte + 1;
				// Selection boundaries must never split a UTF-8 code point.
				while (next < row->end && (static_cast<unsigned char>(text[next]) & 0xc0) == 0x80) ++next;
				const float advance = TextWidth(text, byte, next);
				if (x < cursor + advance * 0.5f) return byte;
				cursor += advance;
				byte = next;
			}
			return row->end;
		};
		if (ImGui::IsItemActivated())
		{
			const auto clicked = position(hit(ImGui::GetIO().MousePos));
			if (!ImGui::GetIO().KeyShift || !m_Anchor.id) m_Anchor = clicked;
			m_Caret = clicked;
		}
		if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			m_Caret = position(hit(ImGui::GetIO().MousePos));
			// Continue selecting when dragging beyond the visible scroll area.
			const auto window = ImGui::GetWindowPos();
			const auto size = ImGui::GetWindowSize();
			const auto mouse = ImGui::GetIO().MousePos;
			const float speed = 500.0f * ImGui::GetIO().DeltaTime;
			if (mouse.y < window.y) ImGui::SetScrollY(ImGui::GetScrollY() - speed);
			if (mouse.y > window.y + size.y) ImGui::SetScrollY(ImGui::GetScrollY() + speed);
			if (mouse.x < window.x) ImGui::SetScrollX(ImGui::GetScrollX() - speed);
			if (mouse.x > window.x + size.x) ImGui::SetScrollX(ImGui::GetScrollX() + speed);
		}
		if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::GetIO().KeyCtrl &&
			ImGui::IsKeyPressed(ImGuiKey_A, false) && !entries.empty())
		{
			m_Anchor = position(0);
			m_Caret = position(text.size());
		}
		const size_t anchor = offset(m_Anchor), caret = offset(m_Caret);
		const size_t selectionBegin = (std::min)(anchor, caret), selectionEnd = (std::max)(anchor, caret);
		const bool selected = anchor != std::string::npos && caret != std::string::npos && anchor != caret;
		auto copy = [&]() {
			if (selected) ImGui::SetClipboardText(text.substr(selectionBegin, selectionEnd - selectionBegin).c_str());
		};
		if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::GetIO().KeyCtrl &&
			ImGui::IsKeyPressed(ImGuiKey_C, false)) copy();
		if (ImGui::BeginPopupContextItem("LogTextActions"))
		{
			if (ImGui::MenuItem("Copy", "Ctrl+C", false, selected)) copy();
			ImGui::EndPopup();
		}
		auto* draw = ImGui::GetWindowDrawList();
		for (const auto& row : rows)
		{
			const ImVec2 pos(origin.x, origin.y + row.y);
			if (!ImGui::IsRectVisible(pos, ImVec2(pos.x + surfaceSize.x, pos.y + lineHeight))) continue;
			if (selected && selectionBegin <= row.end && selectionEnd > row.begin)
			{
				const size_t begin = (std::max)(selectionBegin, row.begin);
				const size_t end = (std::min)(selectionEnd, row.end);
				const float left = TextWidth(text, row.begin, begin);
				const float right = TextWidth(text, row.begin, end) + (selectionEnd > row.end ? ImGui::GetFontSize() * 0.3f : 0.0f);
				draw->AddRectFilled(ImVec2(pos.x + left, pos.y), ImVec2(pos.x + right, pos.y + lineHeight),
					ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
			}
			draw->AddText(pos, ImGui::ColorConvertFloat4ToU32(ColorFor(row.level)),
				text.data() + row.begin, text.data() + row.end);
		}
		if (m_AutoScroll && wasAtBottom && !active && !selected && !rows.empty()) ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();
		ImGui::End();
	}

	static float TextWidth(const std::string& text, size_t begin, size_t end)
	{
		return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f,
			text.data() + begin, text.data() + end).x;
	}
	struct TextPosition { uint64_t id = 0; size_t byte = 0; };
	TextPosition m_Anchor, m_Caret;

	bool m_ShowInfo = true;
	bool m_ShowWarn = true;
	bool m_ShowError = true;
	bool m_AutoScroll = true;
	char m_Filter[128] = {};
};
