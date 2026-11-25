#include "Panels/TextureViewerPanel.h"
#include "Platform/RHI/RHIImGuiRenderer.h"
#include "Core/Assets/AssetDatabase.h"
#include "Core/Threading/ResourceLoader.h"

#include <algorithm>

void TextureViewerPanel::Show(const std::string& path, Ref<RHITexture2D> tex)
{
	if (path != m_Path)
	{
		m_ImportSaved = Engine::AssetDatabase::GetTextureImportSettings(path);
		m_ImportEdit = m_ImportSaved;
		m_ImportError.clear();
	}
	m_Path = path;
	m_Texture = std::move(tex);
	m_Open = true;
}

void TextureViewerPanel::DrawImportSettings()
{
	if (!ImGui::CollapsingHeader("Import Settings", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	auto& edit = m_ImportEdit;
	if (!m_ImportSaved.confirmed)
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.25f, 1.0f),
			"Not confirmed: default settings are in use. Review them and press Apply.");

	ImGui::Checkbox("sRGB", &edit.srgb);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("On: upload as sRGB, the GPU decodes to linear on sampling.\n"
			"Use for color data (base color, emissive).\n"
			"Off for data maps: normal, roughness, metallic, AO, height.");
	ImGui::Checkbox("Flip vertically", &edit.flipY);

	int filter = edit.filter == FilterMode::Nearest ? 1 : 0;
	if (ImGui::Combo("Filter", &filter, "Linear\0Nearest\0"))
		edit.filter = filter == 1 ? FilterMode::Nearest : FilterMode::Linear;

	// Combo order matches WrapMode: Repeat, ClampToEdge, ClampToBorder.
	const char* wrapItems = "Repeat\0Clamp to edge\0Clamp to border\0";
	int wrapS = static_cast<int>(edit.wrapS);
	if (ImGui::Combo("Wrap U", &wrapS, wrapItems))
		edit.wrapS = static_cast<WrapMode>(wrapS);
	int wrapT = static_cast<int>(edit.wrapT);
	if (ImGui::Combo("Wrap V", &wrapT, wrapItems))
		edit.wrapT = static_cast<WrapMode>(wrapT);

	const bool changed = !edit.SameImport(m_ImportSaved);
	ImGui::BeginDisabled(!changed && m_ImportSaved.confirmed);
	if (ImGui::Button("Apply"))
	{
		if (Engine::AssetDatabase::SetTextureImportSettings(m_Path, edit, m_ImportError))
		{
			m_ImportSaved = edit;
			m_ImportSaved.confirmed = true;
			edit.confirmed = true;
			// Reloads in the background; the new texture replaces the old one
			// in every material when it is ready.
			if (changed)
				Engine::ResourceLoader::RequestTextureLoad(m_Path);
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!changed);
	if (ImGui::Button("Revert"))
		edit = m_ImportSaved;
	ImGui::EndDisabled();
	if (!m_ImportError.empty())
		ImGui::TextWrapped("%s", m_ImportError.c_str());
}

TextureViewerPanel*& TextureViewerPanel::Instance()
{
	static TextureViewerPanel* instance = nullptr;
	return instance;
}

void TextureViewerPanel::OnImGuiRender()
{
	if (!m_Open)
	{
		DestroySelf();
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(480.0f, 520.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Texture", &m_Open))
	{
		ImGui::End();
		return;
	}

	const std::string name = m_Path.substr(m_Path.find_last_of("/\\") + 1);
	ImGui::TextUnformatted(name.c_str());

	// Pick up the reloaded texture after import settings are applied.
	if (Ref<RHITexture2D> current = TextureLibiary::Get(m_Path))
		m_Texture = std::move(current);

	DrawImportSettings();
	ImGui::Separator();

	if (!m_Texture || m_Texture->GetWidth() == 0 || m_Texture->GetHeight() == 0)
	{
		ImGui::Separator();
		ImGui::TextUnformatted("Failed to load texture");
		ImGui::End();
		return;
	}

	ImGui::Text("%u x %u", m_Texture->GetWidth(), m_Texture->GetHeight());
	ImGui::Separator();

	const ImTextureID texID = RHIImGUIRenderer::GetTextureID(m_Texture.get());
	if (texID == ImTextureID_Invalid)
	{
		ImGui::TextUnformatted("Preview unavailable");
		ImGui::End();
		return;
	}

	const float srcW = static_cast<float>(m_Texture->GetWidth());
	const float srcH = static_cast<float>(m_Texture->GetHeight());
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float aspect = srcW / srcH;
	float drawW = std::max(avail.x, 1.0f);
	float drawH = drawW / aspect;
	if (drawH > avail.y && avail.y > 1.0f)
	{
		drawH = avail.y;
		drawW = drawH * aspect;
	}
	ImGui::Image(texID, ImVec2(drawW, drawH), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	ImGui::End();
}
