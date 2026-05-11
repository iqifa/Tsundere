#pragma once

#include "ExternalFiles.h"
#include "GLHead.h"
#include "Panels/Material.h"
#include "Panels/MeshFilePath.h"
#include "Input/Input.h"
#include "Widget.h"
#include "Scene/Scene.h"

// ============================================================================
// Inspector free functions
// ============================================================================

inline void RenderTagInspector(Entity& entity)
{
	if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_AllowItemOverlap))
	{
		auto& tag = entity.GetComponent<Tag>();
		char buffer[256];
		memset(buffer, 0, sizeof(buffer));
		strcpy_s(buffer, sizeof(buffer), tag.tag.c_str());
		if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
			tag.tag = std::string(buffer);
	}
}

inline void RenderTransformInspector(Entity& entity)
{
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_AllowItemOverlap))
	{
		auto& t = entity.GetComponent<Transform>();
		ImGui::PushID("##Transform");

		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, 100.0f);
		ImGui::Text("Position");
		ImGui::NextColumn();
		ImGui::DragFloat3("##Pos", &t.Position[0], 0.05f);
		ImGui::NextColumn();

		ImGui::Text("Rotation");
		ImGui::NextColumn();
		ImGui::DragFloat3("##Rot", &t.Rotation[0], 0.05f);
		ImGui::NextColumn();

		ImGui::Text("Scale");
		ImGui::NextColumn();
		ImGui::DragFloat3("##Scale", &t.Scale[0], 0.05f);
		ImGui::Columns(1);
		ImGui::PopID();
	}
}

inline void RenderDirectionalLightInspector(Entity& entity)
{
	ImGui::PushID("##DirLight");

	bool open = ImGui::CollapsingHeader("DirectionalLight",
		ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);

	float btnW = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 2;
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btnW);
	if (ImGui::SmallButton("-"))
	{
		entity.RemoveComponent<DirectionalLight>();
		ImGui::PopID();
		return;
	}

	if (open)
	{
		auto& dl = entity.GetComponent<DirectionalLight>();
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, 100.0f);

		ImGui::Text("Color");
		ImGui::NextColumn();
		ImGui::ColorEdit3("##dlColor", &dl.Color[0]);
		ImGui::NextColumn();

		ImGui::Text("Intensity");
		ImGui::NextColumn();
		ImGui::DragFloat("##dlIntensity", &dl.Intensity, 0.05f, 0.0f, 100.0f);
		ImGui::NextColumn();

		ImGui::Text("Ambient");
		ImGui::NextColumn();
		ImGui::DragFloat("##dlAmbient", &dl.Ambient, 0.01f, 0.0f, 1.0f);
		ImGui::NextColumn();

		ImGui::Text("Direction");
		ImGui::NextColumn();
		ImGui::DragFloat3("##dlDirection", &dl.Direction[0], 0.05f);
		ImGui::Columns(1);
	}

	ImGui::PopID();
}

// --- Persistent state shared by MeshRender & Material inspectors -------------

namespace {
	struct MeshRenderInspectorState
	{
		std::vector<Widget::Widget*> MaterialWidgets;
		bool NeedRebuild = false;
		Entity LastEntity;
		int SelectedMaterialIdx = 0;
	};
	inline MeshRenderInspectorState s_MeshRenderState;

	void ClearMaterialWidgets()
	{
		for (auto* widget : s_MeshRenderState.MaterialWidgets)
			delete widget;
		s_MeshRenderState.MaterialWidgets.clear();
	}

	void BuildMaterialWidgets(Ref<Material> mat)
	{
		ClearMaterialWidgets();

		for (auto& var : mat->varies)
		{
			unsigned int value = std::get<0>(var);
			std::string label = std::get<2>(var);

			switch (std::get<1>(var))
			{
			case ValueType::INT:
				s_MeshRenderState.MaterialWidgets.push_back(new Widget::Input<int>(label, value));
				break;
			case ValueType::FLOAT:
				s_MeshRenderState.MaterialWidgets.push_back(new Widget::Input<float>(label, value));
				break;
			case ValueType::DOUBLE:
				s_MeshRenderState.MaterialWidgets.push_back(new Widget::Input<double>(label, value));
				break;
			case ValueType::VEC2:
				s_MeshRenderState.MaterialWidgets.push_back(new Widget::Input<vec2>(label, value));
				break;
			case ValueType::VEC3:
				s_MeshRenderState.MaterialWidgets.push_back(new Widget::Input<vec3>(label, value));
				break;
			case ValueType::TEXTURE:
				s_MeshRenderState.MaterialWidgets.push_back(new Widget::DiyWidget([label, value]() {
					Texture* tex = (Texture*)value;
					ImGui::Columns(2);
					ImGui::SetColumnWidth(0, 100.0f);
					ImGui::Text(label.c_str());
					ImGui::NextColumn();

					GLuint texID = tex->GetTextureID();
					if (texID)
						ImGui::Image((ImTextureID)(uintptr_t)texID, { 50, 50 }, { 0, 1 }, { 1, 0 });
					else
						ImGui::Text("No Texture");

					ImGui::SameLine();
					if (ImGui::Button("+", { 20, 20 }))
						ImGui::OpenPopup("TextureSelectPopup");

					ImGui::Columns(1);

					if (ImGui::BeginPopup("TextureSelectPopup"))
					{
						for (auto& [path, texture] : TextureLibiary::m_TextureMap)
						{
							if (ImGui::Selectable(path.c_str()))
								*tex = *texture;
						}
						if (ImGui::Button("Close"))
							ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
					}
					}));
				break;
			case ValueType::HEADER:
				s_MeshRenderState.MaterialWidgets.push_back(new Widget::Separator(label));
				break;
			default:
				break;
			}
		}
	}
}

// ============================================================================
// MeshRender inspector — model selection + material list (add/remove)
// ============================================================================

inline void RenderMeshRenderInspector(Entity& entity)
{
	auto& state = s_MeshRenderState;

	if (!(state.LastEntity == entity))
	{
		ClearMaterialWidgets();
		state.SelectedMaterialIdx = 0;
		state.LastEntity = entity;
	}

	if (!entity.HasComponent<MeshRender>())
		return;

	auto& meshrender = entity.GetComponent<MeshRender>();

	ImGui::PushID("##MeshRender");

	bool open = ImGui::CollapsingHeader("MeshRender",
		ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);

	float btnW_minus = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 2;
	float btnW_plus = ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 2;
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btnW_minus - btnW_plus - 4);
	if (ImGui::SmallButton("+"))
	{
		meshrender.materials.push_back(CreateRef<Material>(
			"D:/Code/C++/Tsundere/res/shaders/Lit.shader"));
	}
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btnW_minus);
	if (ImGui::SmallButton("-"))
	{
		entity.RemoveComponent<MeshRender>();
		ImGui::PopID();
		return;
	}

	if (open)
	{
		// --- Model file selector ---
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, 100.0f);

		ImGui::Text("Model");
		ImGui::NextColumn();

		std::string preview = meshrender.ModelPath.empty()
			? "<none>"
			: meshrender.ModelPath.substr(meshrender.ModelPath.find_last_of("/\\") + 1);

		if (ImGui::BeginCombo("##ModelPath", preview.c_str()))
		{
			for (const auto& path : My_map::GetModelPaths())
			{
				bool selected = (path == meshrender.ModelPath);
				std::string display = path.substr(path.find_last_of("/\\") + 1);
				if (ImGui::Selectable(display.c_str(), selected))
				{
					meshrender.ModelPath = path;
					Ref<Model> model = My_map::GetModel(path);
					if (model)
					{
						size_t meshCount = model->meshes.size();
						meshrender.materials.resize(meshCount);
						for (size_t i = 0; i < meshCount; i++)
						{
							if (!meshrender.materials[i])
								meshrender.materials[i] = CreateRef<Material>(
									"D:/Code/C++/Tsundere/res/shaders/Lit.shader");
						}
					}
					state.SelectedMaterialIdx = 0;
					ClearMaterialWidgets();
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::Columns(1);

		// --- Material list per mesh slot ---
		Ref<Model> model = meshrender.ModelPath.empty()
			? nullptr
			: My_map::GetModel(meshrender.ModelPath);

		if (!meshrender.materials.empty())
		{
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("Materials:");

			size_t count = model
				? std::min(model->meshes.size(), meshrender.materials.size())
				: meshrender.materials.size();
			for (size_t i = 0; i < count; i++)
			{
				std::string shaderName = "none";
				if (meshrender.materials[i]
					&& meshrender.materials[i]->shaderindex >= 0
					&& meshrender.materials[i]->shaderindex < (int)My_map::GetShaderPaths().size())
				{
					shaderName = My_map::GetShaderPaths()[meshrender.materials[i]->shaderindex];
					shaderName = shaderName.substr(shaderName.find_last_of("/\\") + 1);
				}

				std::string label = "Mesh[" + std::to_string(i) + "]: " + shaderName;
				bool selected = (state.SelectedMaterialIdx == (int)i);
				if (ImGui::Selectable(label.c_str(), selected))
					state.SelectedMaterialIdx = (int)i;
			}

			if (meshrender.materials.size() > 1)
			{
				ImGui::Spacing();
				if (ImGui::Button("Remove Selected"))
				{
					int idx = state.SelectedMaterialIdx;
					if (idx >= 0 && idx < (int)meshrender.materials.size())
					{
						meshrender.materials.erase(meshrender.materials.begin() + idx);
						if (state.SelectedMaterialIdx >= (int)meshrender.materials.size())
							state.SelectedMaterialIdx = (int)meshrender.materials.size() - 1;
						ClearMaterialWidgets();
					}
				}
			}
		}
	}

	ImGui::PopID();
}

// ============================================================================
// Material inspector — standalone component panel for editing materials
// ============================================================================

inline void RenderMaterialInspector(Entity& entity)
{
	if (!entity.HasComponent<MeshRender>())
		return;

	auto& meshrender = entity.GetComponent<MeshRender>();
	if (meshrender.materials.empty())
		return;

	auto& state = s_MeshRenderState;

	ImGui::PushID("##Material");

	bool open = ImGui::CollapsingHeader("Material",
		ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);

	if (open)
	{
		int idx = state.SelectedMaterialIdx;
		if (idx < 0 || idx >= (int)meshrender.materials.size())
			idx = 0;

		Ref<Material> mat = meshrender.materials[idx];

		if (state.NeedRebuild)
		{
			mat->InitVaires();
			state.NeedRebuild = false;
		}

		// --- Shader selector ---
		if (ImGui::BeginCombo("Shader", My_map::GetShaderPaths()[mat->shaderindex].c_str()))
		{
			for (int i = 0; i < (int)My_map::GetShaderPaths().size(); i++)
			{
				bool selected = (i == mat->shaderindex);
				if (ImGui::Selectable(My_map::GetShaderPaths()[i].c_str(), selected))
				{
					mat->shaderindex = i;
					mat->shader = ShaderLibiray::Get(My_map::GetShaderPaths()[i]);
					state.NeedRebuild = true;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		// --- Uniform widgets ---
		BuildMaterialWidgets(mat);
		for (auto* widget : state.MaterialWidgets)
			widget->_Draw();
	}

	ImGui::PopID();
}

// ============================================================================
// Add component functions
// ============================================================================

inline void AddDirectionalLightComponent(Entity& entity)
{
	if (!entity.HasComponent<DirectionalLight>())
		entity.AddComponent<DirectionalLight>();
	else
		debugwarring("Component DirectionalLight already exists");
}

inline void AddMeshRenderComponent(Entity& entity)
{
	if (!entity.HasComponent<MeshRender>())
	{
		auto& mr = entity.AddComponent<MeshRender>();
		mr.materials.push_back(CreateRef<Material>("D:/Code/C++/Tsundere/res/shaders/Lit.shader"));
		debuglog("AddComponent: MeshRender with default material");
	}
}

inline void AddMaterialToMeshRender(Entity& entity)
{
	if (entity.HasComponent<MeshRender>())
	{
		auto& mr = entity.GetComponent<MeshRender>();
		mr.materials.push_back(CreateRef<Material>("res/shaders/Lit.shader"));
		debuglog("AddComponent: Material added to MeshRender");
	}
	else
	{
		debugwarring("Add MeshRender first before adding Material");
	}
}

// ============================================================================
// Registry — maps keyed by type_hash, populated by static initializers
// ============================================================================

inline std::unordered_map<entt::id_type, std::function<void(Entity&)>> s_RenderFns;
inline std::unordered_map<entt::id_type, std::function<void(Entity&)>> s_AddFns;
inline std::vector<std::string> s_AddableNames;

struct ComponentRegistrar
{
	template<typename T>
	static void Register(const char* name,
		std::function<void(Entity&)> renderFn = nullptr,
		std::function<void(Entity&)> addFn = nullptr)
	{
		auto typeId = entt::type_hash<T>::value();
		entt::meta<T>().type(entt::hashed_string{ name });

		if (renderFn)
			s_RenderFns[typeId] = std::move(renderFn);

		if (addFn)
		{
			s_AddFns[entt::hashed_string{ name }] = std::move(addFn);
			s_AddableNames.push_back(name);
		}
	}
};

inline const auto s_RegMeta_Tag = []() {
	ComponentRegistrar::Register<Tag>("Tag", RenderTagInspector);
	return 0;
}();

inline const auto s_RegMeta_Transform = []() {
	ComponentRegistrar::Register<Transform>("Transform", RenderTransformInspector);
	return 0;
}();

inline const auto s_RegMeta_DirectionalLight = []() {
	ComponentRegistrar::Register<DirectionalLight>("DirectionalLight",
		RenderDirectionalLightInspector, AddDirectionalLightComponent);
	return 0;
}();

inline const auto s_RegMeta_MeshRender = []() {
	ComponentRegistrar::Register<MeshRender>("MeshRender",
		RenderMeshRenderInspector, AddMeshRenderComponent);
	s_AddableNames.push_back("Material");
	return 0;
}();

// ============================================================================
// Runtime discovery helpers — called from Inspect panel
// ============================================================================

inline void DiscoverAndRenderComponents(Entity& entity)
{
	auto h = entt::handle{ entity.m_Scene->m_Registry, static_cast<entt::entity>(entity) };
	for (auto&& [type_id, storage] : h.storage())
	{
		auto it = s_RenderFns.find(type_id);
		if (it != s_RenderFns.end())
			it->second(entity);
	}
}

inline std::vector<std::string> GetAddableComponentNames()
{
	return s_AddableNames;
}

inline void DispatchAddComponent(const std::string& name, Entity& entity)
{
	if (name == "Material")
	{
		AddMaterialToMeshRender(entity);
		return;
	}

	auto id = entt::hashed_string{ name.c_str() };
	auto it = s_AddFns.find(id);
	if (it != s_AddFns.end())
		it->second(entity);
}
