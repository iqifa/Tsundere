#pragma once
#include "Core/Layer/Layer.h"
#include "ExternalFiles.h"
#include "Platform/RHI/RHITextureLibrary.h"
#include "Platform/FileDialog.h"
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
		const auto textures = TextureLibiary::Entries();
		ImGui::Text("Textures (%d)", (int)textures.size());

		ImGui::BeginChild("TextureList", ImVec2(0, 0), true);
		std::string removePath;
		for (auto& [path, tex] : textures)
		{
			ImGui::PushID(path.c_str());

			if (ImGui::SmallButton("X"))
				removePath = path;

			ImGui::SameLine();
			GLuint texID = (GLuint)tex->GetNativeID();
			if (texID)
				ImGui::Image((ImTextureID)(uintptr_t)texID, { 64, 64 }, { 0, 1 }, { 1, 0 });
			else
				ImGui::Dummy({ 64, 64 });

			ImGui::SameLine();
			ImGui::BeginGroup();
			std::string display = path.substr(path.find_last_of("/\\") + 1);
			ImGui::TextUnformatted(display.c_str());
			ImGui::Text("%d x %d", (int)tex->GetWidth(), (int)tex->GetHeight());
			ImGui::EndGroup();

			ImGui::PopID();
		}
		if (!removePath.empty())
			TextureLibiary::Remove(removePath);

		ImGui::EndChild();
		ImGui::End();
	}
};
