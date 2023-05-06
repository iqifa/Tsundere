#pragma once
#ifndef PANEL
#define PANEL

#include"ExternalFiles.h"
#include"HeadLine.h"
#include"Scene/Scene.h"
#include"Scene/Entity.h"
#include"Scene/Component.h"
#include"MeshFilePath.h"

using namespace Component;

class Panel
{
public:
	Panel() = default;
	Panel(const Ref<Scene>& scene)
	{
		SetContext(scene);
	}

	void SetContext(const Ref<Scene>& context) {
		m_Context = context;
		m_SelectedContext = {};
	}
	void SetHeadTitle(string str)
	{
		headtitle = str;
	}
	string headtitle;
	virtual void OnImGUIRender() {

		ImGui::Begin(headtitle.c_str());

		m_Context->m_Registry.each([&](auto entityID) {
			Entity entity{ m_Context.get(),entityID };
			DrawEntityNode(entity);
			});

		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
			m_SelectedContext = {};

		if (ImGui::BeginPopupContextWindow(0, 1))
		{
			if (ImGui::MenuItem("Create Empty Entity"))
			{
				m_Context->CreateEntity("Empty Entity");
			}
			ImGui::EndPopup();
		}

		ImGui::End();

		ImGui::Begin("Proporitise");
		if (m_SelectedContext)
		{
			DrawComponent(m_SelectedContext);
			if (ImGui::Button("Add Component"))
				ImGui::OpenPopup("AddComponent");
			if (ImGui::BeginPopup("AddComponent"))
			{
				if (ImGui::MenuItem("MeshFile"))
				{
					m_SelectedContext.AddComponent<MeshFile>();
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::MenuItem("Material"))
				{
					m_SelectedContext.AddComponent<Material>("res/shaders/default.shader");
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}
		}
		ImGui::End();
	}

private:
	Ref<Scene> m_Context;

	void DrawEntityNode(Entity entity)
	{
		auto& tag = entity.GetComponent<Tag>();

		ImGuiTreeNodeFlags flags = (m_SelectedContext == entity ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
		bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.tag.c_str());

		if (ImGui::IsItemClicked())
		{
			m_SelectedContext = entity;
		}
		bool Deleted = false;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Delete Empty"))
				Deleted = true;
			ImGui::EndPopup();
		}
		if (opened)
		{
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
			bool opened = ImGui::TreeNodeEx((void*)9817239, flags, tag.tag.c_str());
			if (opened)
				ImGui::TreePop();
			ImGui::TreePop();
		}
		if (Deleted)
			m_Context->DestoryEntity(m_SelectedContext);

	};

	static void DrawVec3Control(const string& label, vec3& values, float resetValue = 0.0f, float columWidth = 100.0f)
	{
		ImGui::PushID(label.c_str());

		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columWidth);
		ImGui::Text(label.c_str());
		ImGui::NextColumn();

		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0,0 });

		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec2 buttonSize = { lineHeight + 3.0f,lineHeight };

		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f,0.1f,0.15f,1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f,0.2f,0.20f,1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f,0.1f,0.15f,1.0f });

			if (ImGui::Button("x", buttonSize))
				values.x = resetValue;

			ImGui::PopStyleColor(3);

			ImGui::SameLine();
			ImGui::DragFloat("##x", &values.x, 0.1f);
			ImGui::PopItemWidth();
			ImGui::SameLine();
		}

		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });

			if (ImGui::Button("y", buttonSize))
				values.y = resetValue;

			ImGui::PopStyleColor(3);

			ImGui::SameLine();
			ImGui::DragFloat("##y", &values.y, 0.1f);
			ImGui::PopItemWidth();
			ImGui::SameLine();
		}

		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });

			if (ImGui::Button("z", buttonSize))
				values.z = resetValue;

			ImGui::PopStyleColor(3);

			ImGui::SameLine();
			ImGui::DragFloat("##z", &values.z, 0.1f);
			ImGui::PopItemWidth();
			ImGui::SameLine();
		}

		ImGui::Columns(1);
		ImGui::PopStyleVar();
		ImGui::PopID();
	}
	void DrawComponent(Entity entity)
	{
		if (entity.HasComponent<Tag>())
		{
			auto& tag = entity.GetComponent<Tag>();

			char buffer[256];
			memset(buffer, 0, sizeof(buffer));
			strcpy_s(buffer, sizeof(buffer), tag.tag.c_str());

			if (ImGui::InputText("Tag", buffer, sizeof(buffer)))
			{
				tag.tag = string(buffer);
			}
		}
		if (entity.HasComponent<Transform>())
		{
			auto& transform = entity.GetComponent<Transform>();
			if (ImGui::TreeNodeEx((void*)typeid(Transform).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "TransForm"))
			{

				DrawVec3Control("Position", transform.Position);
				vec3 rotation = degrees(transform.Rotation);
				DrawVec3Control("Rotation", rotation);
				transform.Rotation = radians(rotation);
				DrawVec3Control("Scale", transform.Scale, 1.0f);

				ImGui::TreePop();
			}
		}

		if (entity.HasComponent<MeshFile>())
		{
			if (ImGui::TreeNode("Mesh"))
			{
				auto& component = entity.GetComponent<MeshFile>();

				static int selected = component.m_ModleFile;
				for (int n = 0; n < ModlePaths.size(); n++)
				{
					if (ImGui::Selectable(ModlePaths[n].c_str(), selected == n))
					{
						if (ModleMap::m_ModleMap.find(ModlePaths[n]) != ModleMap::m_ModleMap.end())
						{
							component.m_modle = ModleMap::m_ModleMap[ModlePaths[n]];
						}
						else
						{
							component.m_modle = CreateRef<Model>(ModlePaths[n]);
							ModleMap::m_ModleMap[ModlePaths[n]] = component.m_modle;
						}
						selected = n;
					}
				}
				component.m_ModleFile = selected;
				ImGui::TreePop();
			}
		}

		if (entity.HasComponent<Material>())
		{
			static int item_current = 0;
			using str = char*;
			str* a = new str[ShaderPaths.size()];
			for (int i = 0; i < ShaderPaths.size(); i++)
			{
				a[i] = (str)ShaderPaths[i].c_str();
			}
			ImGui::Combo("shader", &item_current, a, ShaderPaths.size());
		}
	}
	friend class Scene;
	Entity m_SelectedContext;
};

#endif // !PANEL

