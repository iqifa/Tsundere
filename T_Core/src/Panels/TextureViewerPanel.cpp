#include "Panels/TextureViewerPanel.h"
#include "Platform/RHI/RHIImGuiRenderer.h"

#include <algorithm>

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
	ImGui::TextWrapped("%s", m_Path.c_str());

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
