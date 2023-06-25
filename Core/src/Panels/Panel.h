#pragma once
#ifndef PANEL
#define PANEL



#include"ExternalFiles.h"
#include"HeadLine.h"
#include"Scene/Scene.h"
#include"Scene/Entity.h"
#include"Scene/Component.h"
#include"MeshFilePath.h"
#include"Material.h"
#include"Widget.h"

#include<stdio.h>
using namespace Component;


class BasePanel {
public:
	vector<Widget::Widget*> widgets;

	virtual void _Draw_Wdigets()
	{
		for (auto& widget : widgets)
		{
			widget->_Draw();
		}
	}
	template<typename T>
void ValueGUI(string name, unsigned int a)
{
	/*debugerror("Error!:don't set value type");*/
}
template<>
void ValueGUI<int>(string name, unsigned int a)
{
	int* value = (int*)a;
	ImGui::InputInt(name.c_str(), value);
}
template<>
void ValueGUI<float>(string name, unsigned int a)
{
	float* value = (float*)a;
	ImGui::InputFloat(name.c_str(), value);
}
template<>
void ValueGUI<string>(string name, unsigned int a)
{
	string* value = (string*)a;
	char* str = value->data();
	ImGui::InputText(name.c_str(), str, 100);
	*value = str;
}
template<>
void ValueGUI<vec2>(string name, unsigned int a)
{
	vec2* value = (vec2*)a;
	float* aa = (float*)value;
	ImGui::InputFloat2(name.c_str(), aa);
}
template<>
void ValueGUI<vec3>(string name, unsigned int a)
{
	vec3* value = (vec3*)a;
	float* aa = (float*)value;
	ImGui::InputFloat3(name.c_str(), aa);
}
template<>
void ValueGUI<double>(string name, unsigned int a)
{
	double* value = (double*)a;
	ImGui::InputDouble(name.c_str(), value);
}
};

class Panel:public BasePanel
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
	Entity& GetSelected() { return m_SelectedContext; }
	void SetSelected(Entity& entity) { m_SelectedContext = entity; }
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
				if (ImGui::MenuItem("Camera"))
				{
					m_SelectedContext.AddComponent<Camera>(m_SelectedContext.GetComponent<Transform>().Position);
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
		if (ImGui::BeginPopupContextWindow(0,1))
		{
			if (m_SelectedContext==entity&&ImGui::MenuItem("Delete Empty"))
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
			if (ImGui::TreeNode((void*)typeid(MeshFile).hash_code(),"Mesh"))
			{
				auto& component = entity.GetComponent<MeshFile>();

				int selected = component.m_ModleFile;
				for (int n = 0; n < ModlePaths.size(); n++)
				{
					if (ImGui::Selectable(ModlePaths[n].c_str(), selected == n))
					{
						auto& map = My_map::m_ModleMap;
						if (map.find(ModlePaths[n]) != map.end())
						{
							component.m_modle = map[ModlePaths[n]];
						}
						else
						{
							component.m_modle = CreateRef<Model>(ModlePaths[n]);
							map[ModlePaths[n]] = component.m_modle;
						}
						selected = n;
					}
				}
				component.m_ModleFile = selected;

				//if (ImGui::Button("Add Model"))
				//	ImGui::OpenPopup("AddModel");
				//if (ImGui::BeginPopup("AddModel"))
				//{
				//	 ImGui::InputText("")
				//}
				ImGui::TreePop();
			}
		}

		if (entity.HasComponent<Material>())
		{
			auto& material = entity.GetComponent<Material>();
			int item_current = material.shaderindex;
			using str = char*;
			str* a = new str[ShaderPaths.size()];
			for (int i = 0; i < ShaderPaths.size(); i++)
			{
				a[i] = (str)ShaderPaths[i].c_str();
			}
			ImGui::Combo("shader", &item_current, a, ShaderPaths.size());
			if (item_current != material.shaderindex)
			{
				auto& map = My_map::m_ShaderMap;
				if (map.find(ShaderPaths[item_current]) != map.end())
				{
					material.shader = map[ShaderPaths[item_current]];
				}
				else
				{
					material.shader = CreateRef<Shader>(ShaderPaths[item_current]);
					map[ShaderPaths[item_current]] = material.shader;
				}
				material.shaderindex = item_current;
			}
		}
	}
	friend class Scene;
protected:
	Entity m_SelectedContext;
};
class MaterialPanel :public Panel
{
public:
	MaterialPanel() {
		mat = {};
		Ref<Texture>tex = TextureLibiary::Get("res/texture/Sekiro.jpg");
		Widget::ImageRadioButton* img_button=new Widget::ImageRadioButton((void*)tex->GetTextureID(), ImVec2{ 100,100 });
		img_button->ClickEvents += ([]() {
			cout << "wtf" << endl;
			});
		widgets.push_back(img_button);
	}
	Material mat;
public:


	void OnImGUIRender() override {

		if (m_SelectedContext&&m_SelectedContext.HasComponent<Material>())
		{
			mat = m_SelectedContext.GetComponent<Material>();
		}
		else {
			mat = {};
		}
		ImGui::Begin("Material");

		_Draw_Wdigets();
		for (auto value : mat.varies)
		{
			switch (std::get<1>(value))
			{
			case ValueType::INT:ValueGUI<int>(std::get<2>(value), std::get<0>(value));		  break;
			case ValueType::FLOAT:ValueGUI<float>(std::get<2>(value), std::get<0>(value));	  break;
			case ValueType::DOUBLE:ValueGUI<double>(std::get<2>(value), std::get<0>(value));	  break;
			case ValueType::VEC2:ValueGUI<vec2>(std::get<2>(value), std::get<0>(value));		  break;
			case ValueType::VEC3:ValueGUI<vec3>(std::get<2>(value), std::get<0>(value));		  break;
			case ValueType::TEXTURE:
			{
				auto fun = [&]() {
					int* a = (int*)std::get<0>(value);
					int select = *a;

					ImGui::Columns(2);

					ImGui::SetColumnWidth(0, 100.0f);

					ImGui::Columns(1);
					ImGui::Text("Texture");
					ImGui::SameLine();
					ImGui::Text(to_string(*a).c_str());
					ImGui::SameLine();
					if (ImGui::Button("+", ImVec2{ 20,20 }))
						ImGui::OpenPopup("Textures");
					if (ImGui::BeginPopupModal("Textures"))
					{
						auto map = TextureLibiary::m_TextureMap;
						for (auto a : map)
						{
							auto path = a.first;
							auto name = path.substr(path.find_last_of("/"), path.length() - path.find_last_of("/"));

							static bool select = false;
							float width = a.second->GetWidth()*0.3;
							float height = a.second->GetHeight()*0.3;
							if (Widget::SelectButton((void*)a.second->GetTextureID(), ImVec2{ width,height }, select))
							{
								select = !select;
							}
						}

						

						if (ImGui::Button("Close"))
							ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
					}
				}; 
				fun(); break;
			}
			break;
			default:
				break;
			}
		}
		ImGui::End();
	}

	void InitWidget()
	{

	}
};
#endif // !PANEL

