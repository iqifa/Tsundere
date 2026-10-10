#pragma once
#include "Core/Layer/Layer.h"
#include "ExternalFiles.h"
#include "Panels/MeshFilePath.h"
#include "Platform/FileDialog.h"
#include "Core/Threading/ResourceLoader.h"
#include "Scene/BVHBuilder.h"
#include "HeadLine.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

class ModelImportPanel : public Engine::Layer
{
public:
	ModelImportPanel(const std::string& name = "Model Import")
		: Layer(name) {}

	void OnImGuiRender() override
	{
		DrawImportWindow();
		DrawDetailWindow();
	}

private:
	void DrawImportWindow()
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
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Add").x - 20);
		ImGui::InputTextWithHint("##modelpath", "path/to/model.obj", m_PathBuf, sizeof(m_PathBuf));
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::Button("Add"))
		{
			std::string p(m_PathBuf);
			if (!p.empty())
			{
				My_map::AddModelPath(p);
				Engine::ResourceLoader::RequestModelLoad(p);
				m_PathBuf[0] = 0;
			}
		}

		ImGui::Separator();
		ImGui::Text("Model List (%d)", (int)My_map::GetModelPaths().size());

		ImGui::BeginChild("ModelList", ImVec2(0, 0), true);
		int removeIdx = -1;
		for (int i = 0; i < (int)My_map::GetModelPaths().size(); i++)
		{
			const auto& path = My_map::GetModelPaths()[i];
			std::string display = FileName(path);

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
			bool selected = (path == m_SelectedPath);
			if (ImGui::Selectable(display.c_str(), selected))
				OpenDetail(path);

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
			{
				if (paths[removeIdx] == m_SelectedPath)
				{
					m_SelectedPath.clear();
					m_DetailOpen = false;
				}
				My_map::RemoveModelPath(paths[removeIdx]);
			}
		}

		ImGui::EndChild();
		ImGui::End();
	}

	void DrawDetailWindow()
	{
		if (!m_DetailOpen || m_SelectedPath.empty())
			return;

		ImGui::SetNextWindowSize(ImVec2(520.0f, 640.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Model", &m_DetailOpen))
		{
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted(FileName(m_SelectedPath).c_str());
		ImGui::TextWrapped("%s", m_SelectedPath.c_str());

		Ref<Model> model = My_map::GetModel(m_SelectedPath);
		if (!model)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Loading...");
			ImGui::End();
			return;
		}

		if (m_SelectedMesh >= (int)model->meshes.size())
			m_SelectedMesh = 0;

		size_t vertexCount = 0;
		size_t indexCount = 0;
		for (const auto& mesh : model->meshes)
		{
			vertexCount += mesh.vertices.size();
			indexCount += mesh.indices.size();
		}

		ImGui::Separator();
		ImGui::Text("Meshes %d    Vertices %zu    Triangles %zu    Bones %zu",
			(int)model->meshes.size(), vertexCount, indexCount / 3, model->bones.size());

		glm::vec3 boundsMin(0.0f);
		glm::vec3 boundsMax(0.0f);
		if (ComputeBounds(*model, boundsMin, boundsMax))
		{
			ImGui::Text("Bounds min  %.3f  %.3f  %.3f", boundsMin.x, boundsMin.y, boundsMin.z);
			ImGui::Text("Bounds max  %.3f  %.3f  %.3f", boundsMax.x, boundsMax.y, boundsMax.z);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Meshes");
		if (ImGui::BeginTable("##meshes", 4,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
			ImVec2(0, 160)))
		{
			ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthFixed, 50.0f);
			ImGui::TableSetupColumn("Vertices");
			ImGui::TableSetupColumn("Triangles");
			ImGui::TableSetupColumn("GPU");
			ImGui::TableHeadersRow();
			for (int i = 0; i < (int)model->meshes.size(); i++)
			{
				const auto& mesh = model->meshes[i];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				char label[32];
				std::snprintf(label, sizeof(label), "%d", i);
				if (ImGui::Selectable(label, m_SelectedMesh == i, ImGuiSelectableFlags_SpanAllColumns))
					m_SelectedMesh = i;
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%zu", mesh.vertices.size());
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%zu", mesh.indices.size() / 3);
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(mesh.IsGPUReady() ? "ready" : "pending");
			}
			ImGui::EndTable();
		}

		if (!model->meshes.empty())
		{
			const auto& mesh = model->meshes[m_SelectedMesh];
			int skinned = 0;
			for (const auto& v : mesh.vertices)
			{
				if (v.m_Weights[0] != 0.0f)
					skinned++;
			}
			ImGui::Text("Mesh %d    skinned vertices %d / %zu",
				m_SelectedMesh, skinned, mesh.vertices.size());
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Edit vertices");
		ImGui::DragFloat("Scale", &m_Scale, 0.01f, 0.01f, 100.0f);
		ImGui::DragFloat3("Translate", &m_Translate.x, 0.01f);
		if (ImGui::Button("Apply to vertices"))
			ApplyVertexEdit(*model);
		ImGui::SameLine();
		ImGui::TextUnformatted("Reuploads the mesh. Bone offsets stay as loaded.");

		ImGui::Separator();
		ImGui::TextUnformatted("Bones");
		ImGui::InputTextWithHint("##bonefilter", "filter bone name", m_BoneFilter, sizeof(m_BoneFilter));
		if (ImGui::BeginTable("##bones", 2,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
			ImVec2(0, 220)))
		{
			ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
			ImGui::TableSetupColumn("Name");
			ImGui::TableHeadersRow();
			for (int i = 0; i < (int)model->bones.size(); i++)
			{
				auto& bone = model->bones[i];
				if (m_BoneFilter[0] != 0 && bone.name.find(m_BoneFilter) == std::string::npos)
					continue;

				ImGui::PushID(i);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%d", i);
				ImGui::TableSetColumnIndex(1);
				char nameBuf[128];
				std::snprintf(nameBuf, sizeof(nameBuf), "%s", bone.name.c_str());
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::InputText("##bonename", nameBuf, sizeof(nameBuf)))
					bone.name = nameBuf;
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		ImGui::End();
	}

	void OpenDetail(const std::string& path)
	{
		if (path != m_SelectedPath)
		{
			m_SelectedMesh = 0;
			m_Scale = 1.0f;
			m_Translate = glm::vec3(0.0f);
			m_BoneFilter[0] = 0;
		}
		m_SelectedPath = path;
		m_DetailOpen = true;
	}

	static std::string FileName(const std::string& path)
	{
		return path.substr(path.find_last_of("/\\") + 1);
	}

	static bool ComputeBounds(const Model& model, glm::vec3& outMin, glm::vec3& outMax)
	{
		bool any = false;
		glm::vec3 bmin(FLT_MAX);
		glm::vec3 bmax(-FLT_MAX);
		for (const auto& mesh : model.meshes)
		{
			for (const auto& v : mesh.vertices)
			{
				bmin = glm::min(bmin, v.Position);
				bmax = glm::max(bmax, v.Position);
				any = true;
			}
		}
		if (!any)
			return false;
		outMin = bmin;
		outMax = bmax;
		return true;
	}

	void ApplyVertexEdit(Model& model)
	{
		if (m_Scale <= 0.0f)
			return;
		if (m_Scale == 1.0f && m_Translate == glm::vec3(0.0f))
			return;

		for (auto& mesh : model.meshes)
		{
			for (auto& v : mesh.vertices)
				v.Position = v.Position * m_Scale + m_Translate;
			mesh.setupMesh();
		}
		m_Scale = 1.0f;
		m_Translate = glm::vec3(0.0f);
		BVHBuilder::MarkActiveDirty();
	}

	char m_PathBuf[512] = {};
	std::string m_SelectedPath;
	bool m_DetailOpen = false;
	int m_SelectedMesh = 0;
	float m_Scale = 1.0f;
	glm::vec3 m_Translate{ 0.0f };
	char m_BoneFilter[64] = {};
};
