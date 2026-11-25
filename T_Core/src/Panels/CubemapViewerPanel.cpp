#include "Panels/CubemapViewerPanel.h"
#include "Panels/AssetPicker.h"
#include "Panels/TextureViewerPanel.h"
#include "Core/Assets/AssetDatabase.h"
#include "Core/Assets/CubemapAsset.h"
#include "Platform/RHI/RHIImGuiRenderer.h"
#include "Platform/RHI/RHITextureLibrary.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {

// Face order matches the .tcubemap file: +X,-X,+Y,-Y,+Z,-Z.
constexpr const char* kFaceTags[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
constexpr const char* kFaceNames[6] = { "+X Right", "-X Left", "+Y Top", "-Y Bottom", "+Z Front", "-Z Back" };
// (column, row) in the horizontal-cross layout:
//        +Y
//    -X  +Z  +X  -Z
//        -Y
constexpr int kFaceCell[6][2] = { { 2, 1 }, { 0, 1 }, { 1, 0 }, { 1, 2 }, { 1, 1 }, { 3, 1 } };

const ImVec4 kWarnColor(1.0f, 0.7f, 0.25f, 1.0f);

std::string FileName(const std::string& path)
{
	return path.substr(path.find_last_of("/\\") + 1);
}

} // namespace

CubemapViewerPanel*& CubemapViewerPanel::Instance()
{
	static CubemapViewerPanel* instance = nullptr;
	return instance;
}

void CubemapViewerPanel::Show(const std::string& path)
{
	if (path != m_Path)
	{
		m_Path = path;
		m_SaveError.clear();
		Reload();
	}
	m_Open = true;
}

void CubemapViewerPanel::Reload()
{
	m_LoadError.clear();
	m_SavedFaces = {};
	m_SavedUids = {};
	try
	{
		m_SavedUids = Engine::CubemapAsset::ReadUids(m_Path);
		for (size_t i = 0; i < m_SavedFaces.size(); ++i)
			m_SavedFaces[i] = Engine::AssetDatabase::Resolve(m_SavedUids[i], Engine::AssetKind::Texture);
	}
	catch (const std::exception& e)
	{
		m_LoadError = e.what();
	}
	m_EditFaces = m_SavedFaces;
}

void CubemapViewerPanel::OnImGuiRender()
{
	if (!m_Open)
	{
		DestroySelf();
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 640.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Cubemap", &m_Open))
	{
		ImGui::End();
		return;
	}

	ImGui::TextUnformatted(FileName(m_Path).c_str());
	if (!m_LoadError.empty())
	{
		ImGui::Separator();
		ImGui::TextColored(kWarnColor, "%s", m_LoadError.c_str());
		if (ImGui::Button("Retry"))
			Reload();
		ImGui::End();
		return;
	}

	DrawFaces();
	ImGui::Separator();
	DrawFaceEditor();
	ImGui::End();
}

void CubemapViewerPanel::DrawFaces()
{
	constexpr float gap = 4.0f;
	const float avail = ImGui::GetContentRegionAvail().x;
	const float cell = std::clamp(std::floor((avail - gap * 3.0f) / 4.0f), 32.0f, 160.0f);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(cell * 4.0f + gap * 3.0f, cell * 3.0f + gap * 2.0f));

	// Faces must be equal squares; flag the ones that break that.
	uint32_t expected = 0;
	bool consistent = true;
	std::array<Ref<RHITexture2D>, 6> textures;
	for (size_t i = 0; i < textures.size(); ++i)
	{
		if (m_EditFaces[i].empty())
			continue;
		textures[i] = TextureLibiary::Get(m_EditFaces[i]);
		if (!textures[i])
			continue;
		const uint32_t w = textures[i]->GetWidth();
		const uint32_t h = textures[i]->GetHeight();
		if (w != h || (expected != 0 && w != expected))
			consistent = false;
		if (expected == 0)
			expected = w;
	}

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
	for (size_t i = 0; i < 6; ++i)
	{
		const ImVec2 min(origin.x + kFaceCell[i][0] * (cell + gap), origin.y + kFaceCell[i][1] * (cell + gap));
		const ImVec2 max(min.x + cell, min.y + cell);
		dl->AddRectFilled(min, max, IM_COL32(45, 45, 52, 255), 3.0f);

		const auto& tex = textures[i];
		const ImTextureID id = tex ? RHIImGUIRenderer::GetTextureID(tex.get()) : ImTextureID_Invalid;
		if (id != ImTextureID_Invalid)
			dl->AddImage(id, min, max, ImVec2(0, 1), ImVec2(1, 0));
		else
		{
			const char* missing = m_EditFaces[i].empty() ? "missing" : "no preview";
			const ImVec2 size = ImGui::CalcTextSize(missing);
			dl->AddText(ImVec2(min.x + (cell - size.x) * 0.5f, min.y + (cell - size.y) * 0.5f),
				IM_COL32(230, 120, 100, 255), missing);
		}

		const bool bad = m_EditFaces[i].empty() ||
			(tex && (tex->GetWidth() != tex->GetHeight() || (expected && tex->GetWidth() != expected)));
		dl->AddRect(min, max, bad ? IM_COL32(230, 120, 100, 255) : IM_COL32(90, 90, 100, 255), 3.0f, 0,
			bad ? 2.0f : 1.0f);
		dl->AddRectFilled(ImVec2(min.x + 2, min.y + 2),
			ImVec2(min.x + 6 + ImGui::CalcTextSize(kFaceTags[i]).x, min.y + 4 + ImGui::GetTextLineHeight()),
			IM_COL32(0, 0, 0, 160), 2.0f);
		dl->AddText(ImVec2(min.x + 4, min.y + 3), textColor, kFaceTags[i]);

		if (ImGui::IsMouseHoveringRect(min, max) && ImGui::IsWindowHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(kFaceNames[i]);
			if (m_EditFaces[i].empty())
				ImGui::TextColored(kWarnColor, "Texture UID %llu not found",
					static_cast<unsigned long long>(m_SavedUids[i]));
			else
			{
				ImGui::TextUnformatted(FileName(m_EditFaces[i]).c_str());
				if (tex)
					ImGui::Text("%u x %u", tex->GetWidth(), tex->GetHeight());
			}
			ImGui::EndTooltip();
		}
	}

	if (expected != 0)
		ImGui::Text("Face size %u x %u", expected, expected);
	if (!consistent)
		ImGui::TextColored(kWarnColor, "Faces must be square and all the same size.");
}

void CubemapViewerPanel::DrawFaceEditor()
{
	if (!ImGui::CollapsingHeader("Faces", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	for (size_t i = 0; i < m_EditFaces.size(); ++i)
	{
		ImGui::PushID(static_cast<int>(i));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(kFaceNames[i]);
		ImGui::SameLine();
		const float buttonWidth = ImGui::CalcTextSize("Import Settings").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth(std::max(60.0f,
			ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x));
		const auto picked = AssetPicker::Texture("##Texture", m_EditFaces[i]);
		if (!picked.empty())
			m_EditFaces[i] = picked;
		ImGui::SameLine();
		ImGui::BeginDisabled(m_EditFaces[i].empty());
		if (ImGui::Button("Import Settings"))
			TextureViewerPanel::Open(m_EditFaces[i]);
		ImGui::EndDisabled();
		ImGui::PopID();
	}

	const bool dirty = m_EditFaces != m_SavedFaces;
	ImGui::BeginDisabled(!dirty);
	if (ImGui::Button("Save"))
	{
		try
		{
			Engine::CubemapAsset::Save(m_EditFaces, m_Path);
			Reload();
			m_SaveError.clear();
			// Users of this cubemap (e.g. the skybox) watch this and reload.
			Engine::AssetDatabase::MarkContentChanged(
				Engine::AssetDatabase::UidForPath(m_Path, Engine::AssetKind::Cubemap));
		}
		catch (const std::exception& e)
		{
			m_SaveError = e.what();
			Error_Core("[Cubemap save]: {}", e.what());
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Revert"))
	{
		m_EditFaces = m_SavedFaces;
		m_SaveError.clear();
	}
	ImGui::EndDisabled();
	if (!m_SaveError.empty())
		ImGui::TextWrapped("%s", m_SaveError.c_str());
	ImGui::TextDisabled("Faces must be textures imported into Assets.");
}
