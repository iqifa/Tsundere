#pragma once
#ifndef PANEL
#define PANEL



#include"Core/Layer/Layer.h"
#include"Scene/Component.h"
#include <Widget.h>
using namespace Component;


class BasePanel :public Engine::Layer{
public:
	std::vector<Widget::Widget*> widgets;

	BasePanel() = default;
	BasePanel(std::string Title) :Layer(Title) { m_HeadTitle = Title; }

	virtual void _Draw_Wdigets()
	{
		for (auto& widget : widgets)
		{
			widget->_Draw();
		}
	}

	virtual void SetHeadTile(std::string Title) { m_HeadTitle = Title; };

	std::string m_HeadTitle;
};

//class Panel:public BasePanel
//{
//public:
//	Panel() = default;
//	Panel(const Ref<Scene>& scene)
//	{
//		SetContext(scene);
//	}
//
//	void SetContext(const Ref<Scene>& context) {
//		m_Context = context;
//		m_SelectedContext = {};
//	}
//	void SetHeadTitle(std::string str)
//	{
//		headtitle = str;
//	}
//	Entity& GetSelected() { return m_SelectedContext; }
//	void SetSelected(Entity& entity) { m_SelectedContext = entity; }
//	std::string headtitle;
//	virtual void OnImGUIRender() {
//
//		ImGui::Begin(headtitle.c_str());
//
//		/*m_Context->m_Registry.each([&](auto entityID) {
//			Entity entity{ m_Context.get(),entityID };
//			DrawEntityNode(entity);
//			});*/
//
//		for (auto entityID : m_Context->m_Registry.view<entt::entity>())
//		{
//			Entity entity{ m_Context.get(),entityID };
//			DrawEntityNode(entity);
//		}
//
//		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
//			m_SelectedContext = {};
//
//		if (ImGui::BeginPopupContextWindow(0, 1))
//		{
//			if (ImGui::MenuItem("Create Empty Entity"))
//			{
//				m_Context->CreateEntity("Empty Entity");
//			}
//			ImGui::EndPopup();
//		}
//
//		ImGui::End();
//
//		ImGui::Begin("Inspect");
//		if (m_SelectedContext)
//		{
//			DrawComponent(m_SelectedContext);
//			if (ImGui::Button("Add Component"))
//				ImGui::OpenPopup("AddComponent");
//			if (ImGui::BeginPopup("AddComponent"))
//			{
//				if (ImGui::MenuItem("MeshFile"))
//				{
//					m_SelectedContext.AddComponent<MeshFile>();
//					ImGui::CloseCurrentPopup();
//				}
//
//				if (ImGui::MenuItem("Material"))
//				{
//					m_SelectedContext.AddComponent<Material>("res/shaders/default.shader");
//					ImGui::CloseCurrentPopup();
//				}
//				if (ImGui::MenuItem("Camera"))
//				{
//					m_SelectedContext.AddComponent<Camera>(m_SelectedContext.GetComponent<Transform>().Position);
//					ImGui::CloseCurrentPopup();
//				}
//				if (ImGui::MenuItem("MeshRender"))
//				{
//					m_SelectedContext.AddComponent<MeshRender>();
//					ImGui::CloseCurrentPopup();
//				}
//				ImGui::EndPopup();
//			}
//		}
//		ImGui::End();
//	}
//private:
//	Ref<Scene> m_Context;
//
//	void DrawEntityNode(Entity entity)
//	{
//		auto& tag = entity.GetComponent<Tag>();
//
//		ImGuiTreeNodeFlags flags = (m_SelectedContext == entity ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
//		bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.tag.c_str());
//
//		if (ImGui::IsItemClicked())
//		{
//			m_SelectedContext = entity;
//		}
//		bool Deleted = false;
//		if (ImGui::BeginPopupContextWindow(0,1))
//		{
//			if (m_SelectedContext==entity&&ImGui::MenuItem("Delete Empty"))
//				Deleted = true;
//			ImGui::EndPopup();
//		}
//		if (opened)
//		{
//			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
//			bool opened = ImGui::TreeNodeEx((void*)9817239, flags, tag.tag.c_str());
//			if (opened)
//				ImGui::TreePop();
//			ImGui::TreePop();
//		}
//		if (Deleted)
//			m_Context->DestoryEntity(m_SelectedContext);
//
//	};
//
//	static void DrawVec3Control(const std::string& label, vec3& values, float resetValue = 0.0f, float columWidth = 100.0f)
//	{
//		ImGui::PushID(label.c_str());
//
//		ImGui::Columns(2);
//		ImGui::SetColumnWidth(0, columWidth);
//		ImGui::Text(label.c_str());
//		ImGui::NextColumn();
//
//		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
//		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0,0 });
//
//		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
//		ImVec2 buttonSize = { lineHeight + 3.0f,lineHeight };
//
//		{
//			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f,0.1f,0.15f,1.0f });
//			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f,0.2f,0.20f,1.0f });
//			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f,0.1f,0.15f,1.0f });
//
//			if (ImGui::Button("x", buttonSize))
//				values.x = resetValue;
//
//			ImGui::PopStyleColor(3);
//
//			ImGui::SameLine();
//			ImGui::DragFloat("##x", &values.x, 0.1f);
//			ImGui::PopItemWidth();
//			ImGui::SameLine();
//		}
//
//		{
//			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
//			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
//			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
//
//			if (ImGui::Button("y", buttonSize))
//				values.y = resetValue;
//
//			ImGui::PopStyleColor(3);
//
//			ImGui::SameLine();
//			ImGui::DragFloat("##y", &values.y, 0.1f);
//			ImGui::PopItemWidth();
//			ImGui::SameLine();
//		}
//
//		{
//			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
//			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
//			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
//
//			if (ImGui::Button("z", buttonSize))
//				values.z = resetValue;
//
//			ImGui::PopStyleColor(3);
//
//			ImGui::SameLine();
//			ImGui::DragFloat("##z", &values.z, 0.1f);
//			ImGui::PopItemWidth();
//			ImGui::SameLine();
//		}
//
//		ImGui::Columns(1);
//		ImGui::PopStyleVar();
//		ImGui::PopID();
//	}
//	void DrawComponent(Entity entity)
//	{
//		if (entity.HasComponent<Tag>())
//		{
//			auto& tag = entity.GetComponent<Tag>();
//
//			char buffer[256];
//			memset(buffer, 0, sizeof(buffer));
//			strcpy_s(buffer, sizeof(buffer), tag.tag.c_str());
//
//			if (ImGui::InputText("Tag", buffer, sizeof(buffer)))
//			{
//				tag.tag = std::string(buffer);
//			}
//		}
//		if (entity.HasComponent<Transform>())
//		{
//			auto& transform = entity.GetComponent<Transform>();
//			if (ImGui::TreeNodeEx((void*)typeid(Transform).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "TransForm"))
//			{
//
//				DrawVec3Control("Position", transform.Position);
//				vec3 rotation = degrees(transform.Rotation);
//				DrawVec3Control("Rotation", rotation);
//				transform.Rotation = radians(rotation);
//				DrawVec3Control("Scale", transform.Scale, 1.0f);
//
//				ImGui::TreePop();
//			}
//		}
//
//		if (entity.HasComponent<MeshFile>())
//		{
//			if (ImGui::TreeNode((void*)typeid(MeshFile).hash_code(),"Mesh"))
//			{
//				auto& component = entity.GetComponent<MeshFile>();
//
//				int selected = component.m_ModleFile;
//				for (int n = 0; n < ModlePaths.size(); n++)
//				{
//					if (ImGui::Selectable(ModlePaths[n].c_str(), selected == n))
//					{
//						auto& map = My_map::m_ModleMap;
//						if (map.find(ModlePaths[n]) != map.end())
//						{
//							component.m_modle = map[ModlePaths[n]];
//						}
//						else
//						{
//							component.m_modle = CreateRef<Model>(ModlePaths[n]);
//							map[ModlePaths[n]] = component.m_modle;
//						}
//						selected = n;
//					}
//				}
//				component.m_ModleFile = selected;
//
//				//if (ImGui::Button("Add Model"))
//				//	ImGui::OpenPopup("AddModel");
//				//if (ImGui::BeginPopup("AddModel"))
//				//{
//				//	 ImGui::InputText("")
//				//}
//				ImGui::TreePop();
//			}
//		}
//
//		if (entity.HasComponent<Material>())
//		{
//			auto& material = entity.GetComponent<Material>();
//			int item_current = material.shaderindex;
//			using str = char*;
//			str* a = new str[ShaderPaths.size()];
//			for (int i = 0; i < ShaderPaths.size(); i++)
//			{
//				a[i] = (str)ShaderPaths[i].c_str();
//			}
//			ImGui::Combo("shader2", &material.shaderindex, a, ShaderPaths.size());
//
//			//TODO: ShaderPath---->a
//			if (item_current != material.shaderindex && item_current >= 0)
//			{
//				auto& map = My_map::m_ShaderMap;
//				if (map.find(ShaderPaths[item_current]) != map.end())
//				{
//					material.shader = map[ShaderPaths[item_current]];
//				}
//				else
//				{
//					material.shader = CreateRef<Shader>(ShaderPaths[item_current]);
//					map[ShaderPaths[item_current]] = material.shader;
//				}
//			}
//		}
//		if (entity.HasComponent<MeshRender>())
//		{
//			auto& meshrender = entity.GetComponent<MeshRender>();
//			if (ImGui::TreeNodeEx((void*)typeid(MeshRender).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "MeshRender"))
//			{
//				std::string Lable="aaa"; 
//				static bool Selected;
//				ImVec2 size = ImVec2(200, 200);
//				ImGuiWindow* window = ImGui::GetCurrentWindow();
//				ImVec2 pos = window->DC.CursorPos;
//				Ref<Texture>tex = TextureLibiary::Get("res/texture/Sekiro.jpg");
//				ImGuiContext& g = *GImGui;
//				const ImVec2 padding = g.Style.FramePadding;
//				const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size /*+ padding * 2.0f*/);
//				ImGui::Selectable(("##Select_" + Lable).c_str(), &Selected, ImGuiSelectableFlags_None, size);
//				window->DrawList->AddImage((void*)tex->GetTextureID(), bb.Min /*+ padding*/, bb.Max /*- padding*/);
//				
//				//ImGui::LabelText(("##lable_" + Lable).c_str(), Lable.c_str());
//				ImGui::SetItemAllowOverlap();
//				ImGui::RenderText(pos, Lable.c_str());
//				
//				//window->DrawList->AddImage((void*)tex->GetTextureID(), bb.Min + padding, bb.Max - padding);
//				//ImGui::Image((void*)tex->GetTextureID(),size);
//				ImGui::TreePop();
//			}
//			
//		}
//	}
//	friend class Scene;
//protected:
//	Entity m_SelectedContext;
//};
//class MaterialPanel :public Panel
//{
//public:
//	MaterialPanel() {
//		show = new bool;
//		mat = {};
//		Ref<Texture>tex = TextureLibiary::Get("res/texture/Sekiro.jpg");
//		Widget::ImageRadioButton* img_button=new Widget::ImageRadioButton((void*)tex->GetTextureID(), ImVec2{ 100,100 });
//		Widget::Image_Select* img_select = new Widget::Image_Select("##Sekiro", ImVec2{ 200,200 }, tex);
//		img_button->ClickEvents += ([]() {
//			cout << "wtf" << endl;
//			});
//		widgets.push_back(img_button);
//		widgets.push_back(img_select);
//
//		Eventing::Event<> Events;
//		Events += []() {
//			cout << "Add Event!!!" << endl;
//		};
//		Events.Invoke();
//		Eventing::Event<int,float>Events2;
//
//		Events2 += [](int a, float b) {
//			cout << "Events2 a+b" << endl;
//			cout << a + b << endl;
//		};
//		Events2 += [](int a, float b) {
//			cout << "Events2 a-b" << endl;
//			cout << a - b << endl;
//		};
//		Events2.Invoke(2, 3);
//	}
//	Material mat;
//	bool* show;
//public:
//
//
//	void OnImGUIRender() override {
//
//		if (m_SelectedContext != m_LastSelect)
//		{
//			debugwarring("Change Selected");
//			for (auto& widget : PanelWiget)
//			{
//					free(widget);
//			}
//			PanelWiget.clear();
//			m_LastSelect = m_SelectedContext;
//
//			if (m_SelectedContext && m_SelectedContext.HasComponent<Material>())
//			{
//				
//				Material& material = m_SelectedContext.GetComponent<Material>();
//				PanelWiget.push_back(new Widget::TCombo("Shader", &material.shaderindex, ShaderPaths, [&]() {
//					material.shader = ShaderLibiray::Get(ShaderPaths[material.shaderindex]);
//					material.InitVaires();
//					this->mat = material;
//					ClearWidgets();
//					InitWidget();
//					}));
//				mat = m_SelectedContext.GetComponent<Material>();
//			}
//			else {
//				mat = {};
//				//*show = false;
//			}
//			ClearWidgets();
//			InitWidget();
//		}
//		if (*show)
//		{
//			ImGui::Begin("Material", show);
//
//			for (auto& widget : PanelWiget)
//			{
//				widget->_Draw();
//			}
//			_Draw_Wdigets();
//
//			ImGui::End();
//		}
//	}
//
//	void InitWidget()
//	{
//		for (auto& var : mat.varies)
//		{
//			unsigned int value = std::get<0>(var);
//			string lable = std::get<2>(var);
//			switch (std::get<1>(var))
//			{
//			case ValueType::INT: 	  widgets.push_back(new Widget::Input<int>(lable, value));    break;
//			case ValueType::FLOAT:	  widgets.push_back(new Widget::Input<float>(lable,value));   break;
//			case ValueType::DOUBLE:	  widgets.push_back(new Widget::Input<double>(lable, value)); break;
//			case ValueType::VEC2:	  widgets.push_back(new Widget::Input<vec2>(lable, value));   break;
//			case ValueType::VEC3:	  widgets.push_back(new Widget::Input<double>(lable, value)); break;
//			case ValueType::TEXTURE:  widgets.push_back(new Widget::DiyWidget([&, lable]() {
//				Texture* tex = (Texture*)std::get<0>(var);
//				ImGui::Columns(2);
//				ImGui::SetColumnWidth(0, 100.0f);
//				ImVec2 Pos = ImGui::GetCursorScreenPos();
//				float Height = ImGui::GetTextLineHeight();
//				ImVec2 strsize=ImGui::CalcTextSize(lable.c_str());
//				Pos.y += (50 - Height) * 0.5;
//				Pos.x += (100 - strsize.x) * 0.5;
//				ImGui::RenderText(Pos, lable.c_str());
//				//ImGui::Text("Texture");
//				ImGui::NextColumn();
//
//				//ImGui::RenderText(Pos, "Test");W
//				ImGui::Image((void*)tex->GetTextureID(), { 50,50 }, { 0,1 }, { 1,0 });
//				ImGui::SameLine();
//				
//				Pos= ImGui::GetCursorScreenPos();
//				Pos.y += 15;
//				ImGui::SetCursorScreenPos(Pos);
//				if (ImGui::Button("+", ImVec2{ 20,20 }))
//					ImGui::OpenPopup("Textures");
//				ImGui::Columns(1);
//				if (ImGui::BeginPopupModal("Textures"))
//				{
//					auto map = TextureLibiary::m_TextureMap;
//					for (auto a : map)
//					{
//						auto path = a.first;
//						auto name = path.substr(path.find_last_of("/"), path.length() - path.find_last_of("/"));
//
//						static bool select = false;
//						float width = a.second->GetWidth() * 0.3;
//						float height = a.second->GetHeight() * 0.3;
//					}
//
//
//
//					if (ImGui::Button("Close"))
//						ImGui::CloseCurrentPopup();
//					if (ImGui::Button("add"))
//						*tex = *TextureLibiary::Get("res/texture/Sekiro.jpg");
//					ImGui::EndPopup();
//				}
//				}	));	  break;
//			case ValueType::HEADER:	  widgets.push_back(new Widget::Separator(lable));
//			default:
//				break;
//			}
//		}
//	}
//
//	void ClearWidgets() {
//		for (auto value : widgets)
//		{
//			free(value);
//		}
//		widgets.clear();
//	}
//private:
//	Entity m_LastSelect;
//	std::vector<Widget::Widget*> PanelWiget;
//};
#endif // !PANEL