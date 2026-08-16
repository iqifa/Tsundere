#pragma once
#include "Core/Layer/Layer.h"
#include "ExternalFiles.h"
#include "Platform/GL/Texture.h"
#include "Platform/GL/FileDialog.h"
#include "Core/Threading/ResourceLoader.h"
#include "HeadLine.h"

class TextureImportPanel : public Engine::Layer
{
public:
	TextureImportPanel(const std::string& name = "Texture Import")
		: Layer(name) {}

	void OnImGuiRender() override
	{
		ImGui::Begin("Texture Import");

		if (ImGui::Button("Browse..."))
		{
			std::string path = OpenTextureFileDialog();
			if (!path.empty())
				Engine::ResourceLoader::RequestTextureLoad(path);
		}

		ImGui::SameLine();
		static char pathBuf[512] = "";
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Add").x - 20);
		ImGui::InputTextWithHint("##texpath", "path/to/texture.png", pathBuf, sizeof(pathBuf));
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::Button("Add"))
		{
			std::string p(pathBuf);
			if (!p.empty())
			{
				Engine::ResourceLoader::RequestTextureLoad(p);
				pathBuf[0] = 0;
			}
		}

		ImGui::Separator();
		ImGui::Text("Textures (%d)", (int)TextureLibiary::m_TextureMap.size());

		ImGui::BeginChild("TextureList", ImVec2(0, 0), true);
		std::string removePath;
		for (auto& [path, tex] : TextureLibiary::m_TextureMap)
		{
			ImGui::PushID(path.c_str());

			if (ImGui::SmallButton("X"))
				removePath = path;

			ImGui::SameLine();
			GLuint texID = tex->GetTextureID();
			if (texID)
				ImGui::Image((ImTextureID)(uintptr_t)texID, { 64, 64 }, { 0, 1 }, { 1, 0 });
			else
				ImGui::Dummy({ 64, 64 });

			ImGui::SameLine();
			ImGui::BeginGroup();
			std::string display = path.substr(path.find_last_of("/\\") + 1);
			ImGui::TextUnformatted(display.c_str());
			ImGui::Text("%d x %d", tex->GetWidth(), tex->GetHeight());
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(path.c_str());
				ImGui::EndTooltip();
			}
			ImGui::EndGroup();

			ImGui::PopID();
		}
		if (!removePath.empty())
		{
			std::unique_lock<std::shared_mutex> lock(TextureLibiary::s_Mutex);
			TextureLibiary::m_TextureMap.erase(removePath);
		}

		ImGui::EndChild();
		ImGui::End();
	}
};
