#pragma once
#include "Core/Layer/Layer.h"
#include "ExternalFiles.h"
#include "Panels/MeshFilePath.h"
#include "Platform/GL/FileDialog.h"
#include "Core/Threading/ResourceLoader.h"
#include "HeadLine.h"

class ModelImportPanel : public Engine::Layer
{
public:
	ModelImportPanel(const std::string& name = "Model Import")
		: Layer(name) {}

	void OnImGuiRender() override
	{
		ImGui::Begin("Model Import");

		if (ImGui::Button("Browse..."))
		{
			std::string path = OpenModelFileDialog();
			if (!path.empty())
			{
				My_map::AddModelPath(path);
				Engine::ResourceLoader::RequestModelLoad(path);
			}
		}

		ImGui::SameLine();
		static char pathBuf[512] = "";
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Add").x - 20);
		ImGui::InputTextWithHint("##modelpath", "path/to/model.obj", pathBuf, sizeof(pathBuf));
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::Button("Add"))
		{
			std::string p(pathBuf);
			if (!p.empty())
			{
				My_map::AddModelPath(p);
				Engine::ResourceLoader::RequestModelLoad(p);
				pathBuf[0] = 0;
			}
		}

		ImGui::Separator();
		ImGui::Text("Model List (%d)", (int)My_map::GetModelPaths().size());

		ImGui::BeginChild("ModelList", ImVec2(0, 0), true);
		int removeIdx = -1;
		for (int i = 0; i < (int)My_map::GetModelPaths().size(); i++)
		{
			const auto& path = My_map::GetModelPaths()[i];
			std::string display = path.substr(path.find_last_of("/\\") + 1);

			auto it = My_map::m_ModleMap.find(path);
			bool loaded = (it != My_map::m_ModleMap.end());
			std::string info;
			if (loaded)
				info = "[loaded] " + std::to_string(it->second->meshes.size()) + " meshes";
			else
				info = "[not loaded]";

			ImGui::PushID(i);

			if (ImGui::SmallButton("X"))
				removeIdx = i;
			ImGui::SameLine();
			ImGui::TextUnformatted(info.c_str());
			ImGui::SameLine();
			ImGui::Selectable(display.c_str());

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(path.c_str());
				ImGui::EndTooltip();
			}

			ImGui::PopID();
		}
		if (removeIdx >= 0)
		{
			auto paths = My_map::GetModelPaths();
			if (removeIdx < (int)paths.size())
				My_map::RemoveModelPath(paths[removeIdx]);
		}

		ImGui::EndChild();
		ImGui::End();
	}
};
