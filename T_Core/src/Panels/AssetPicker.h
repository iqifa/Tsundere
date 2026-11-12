#pragma once

#include "Core/Assets/AssetDatabase.h"
#include "Platform/RHI/RHIImGuiRenderer.h"
#include "Platform/RHI/RHITextureLibrary.h"

#include <algorithm>
#include <unordered_set>

namespace AssetPicker
{
	inline std::string Name(const std::string& path)
	{
		return path.substr(path.find_last_of("/\\") + 1);
	}

	inline std::string Texture(const char* label, const std::string& currentPath, bool includeCached = false)
	{
		const auto name = Name(currentPath);
		ImGui::SetNextWindowSizeConstraints(ImVec2(260.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		if (!ImGui::BeginCombo(label, name.empty() ? "(none)" : name.c_str(), ImGuiComboFlags_HeightLarge))
			return {};

		const bool opening = ImGui::IsWindowAppearing();
		const std::string currentKey = currentPath.empty()
			? std::string{} : Engine::AssetDatabase::NormalizePath(currentPath);
		std::unordered_set<std::string> seen;
		std::string picked;
		const auto draw = [&](const std::string& path, Ref<RHITexture2D> texture)
		{
			const std::string key = Engine::AssetDatabase::NormalizePath(path);
			if (!seen.insert(key).second)
				return;

			ImGui::PushID(key.c_str());
			const ImGuiID failed = ImGui::GetID("PreviewUnavailable");
			auto* storage = ImGui::GetStateStorage();
			if (opening)
				storage->SetBool(failed, false);

			constexpr float thumbnail = 48.0f;
			const auto display = Name(path);
			const ImVec2 labelPosition = ImGui::GetCursorScreenPos();
			const float labelIndent = thumbnail + 10.0f;
			const bool selected = key == currentKey;
			if (ImGui::Selectable("##Texture", selected, ImGuiSelectableFlags_SpanAvailWidth,
				ImVec2(labelIndent + ImGui::CalcTextSize(display.c_str()).x, thumbnail)))
				picked = path;
			if (selected)
				ImGui::SetItemDefaultFocus();
			if (ImGui::IsItemVisible())
			{
				if (!texture && !storage->GetBool(failed))
				{
					texture = TextureLibiary::Get(path);
					storage->SetBool(failed, !texture);
				}
				const auto id = texture ? RHIImGUIRenderer::GetTextureID(texture.get()) : ImTextureID_Invalid;
				auto* drawList = ImGui::GetWindowDrawList();
				const ImVec2 rowMin = ImGui::GetItemRectMin();
				const ImVec2 rowMax = ImGui::GetItemRectMax();
				if (id != ImTextureID_Invalid && texture->GetWidth() && texture->GetHeight())
				{
					const float width = static_cast<float>(texture->GetWidth());
					const float height = static_cast<float>(texture->GetHeight());
					const float scale = thumbnail / std::max(width, height);
					const ImVec2 start(labelPosition.x + (thumbnail - width * scale) * 0.5f,
						(rowMin.y + rowMax.y - height * scale) * 0.5f);
					drawList->AddImage(id, start, ImVec2(start.x + width * scale, start.y + height * scale),
						ImVec2(0, 1), ImVec2(1, 0));
				}
				drawList->AddText(ImVec2(labelPosition.x + labelIndent,
					(rowMin.y + rowMax.y - ImGui::GetTextLineHeight()) * 0.5f),
					ImGui::GetColorU32(ImGuiCol_Text), display.c_str());
			}
			ImGui::PopID();
		};

		for (const auto& asset : Engine::AssetDatabase::GetByKind(Engine::AssetKind::Texture))
			draw(asset.path, nullptr);
		if (includeCached)
			for (const auto& [path, texture] : TextureLibiary::Entries())
				draw(path, texture);
		ImGui::EndCombo();
		return picked;
	}
}
