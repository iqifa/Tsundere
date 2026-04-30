#include "Inspect.h"
#include"Input/Input.h"

Inspect::Inspect(Ref<Scene> scene, std::string name) :BasePanel(name)
{
	m_Context = scene;
	componentname.push_back("MeshRender");
	componentname.push_back("Transform");
	componentname.push_back("Material");
}

void Inspect::OnUpdate()
{
}

void Inspect::ClearMaterialWidgets()
{
	for (auto* widget : m_MaterialWidgets)
		delete widget;
	m_MaterialWidgets.clear();
}

void Inspect::BuildMaterialWidgets(Ref<Material> mat)
{
	ClearMaterialWidgets();

	for (auto& var : mat->varies)
	{
		unsigned int value = std::get<0>(var);
		std::string label = std::get<2>(var);

		switch (std::get<1>(var))
		{
		case ValueType::INT:
			m_MaterialWidgets.push_back(new Widget::Input<int>(label, value));
			break;
		case ValueType::FLOAT:
			m_MaterialWidgets.push_back(new Widget::Input<float>(label, value));
			break;
		case ValueType::DOUBLE:
			m_MaterialWidgets.push_back(new Widget::Input<double>(label, value));
			break;
		case ValueType::VEC2:
			m_MaterialWidgets.push_back(new Widget::Input<vec2>(label, value));
			break;
		case ValueType::VEC3:
			m_MaterialWidgets.push_back(new Widget::Input<vec3>(label, value));
			break;
		case ValueType::TEXTURE:
			m_MaterialWidgets.push_back(new Widget::DiyWidget([label, value]() {
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
			m_MaterialWidgets.push_back(new Widget::Separator(label));
			break;
		default:
			break;
		}
	}
}

void Inspect::OnImGuiRender()
{
	ImGui::Begin(m_HeadTitle.c_str());

	if (m_SelectedContext != null)
	{
		Entity entity{ m_Context.get(), m_SelectedContext };

		if (entity.HasComponent<Tag>())
		{
			if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_AllowItemOverlap)) {
				auto& tag = entity.GetComponent<Tag>();

				char buffer[256];
				memset(buffer, 0, sizeof(buffer));
				strcpy_s(buffer, sizeof(buffer), tag.tag.c_str());

				if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
				{
					tag.tag = std::string(buffer);
				}
			}
		}

		if (entity.HasComponent<Transform>())
		{
			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_AllowItemOverlap))
			{
			}
			float win_width = ImGui::GetWindowWidth();
			ImGui::SameLine(win_width - 20);

			if (ImGui::Button("-"))
			{
				debugerror(entity.GetComponent<Tag>().tag + " Del Component Transform")
			}
		}

		// Material / MeshRender section
		if (entity.HasComponent<MeshRender>())
		{
			auto& meshrender = entity.GetComponent<MeshRender>();

			if (ImGui::CollapsingHeader("MeshRender", ImGuiTreeNodeFlags_DefaultOpen))
			{
				Ref<Material> mat = meshrender.materials[0];

				// Handle deferred shader change
				if (m_NeedRebuild)
				{
					mat->InitVaires();
					m_NeedRebuild = false;
				}

				// Shader selection combo
				std::string shaderLabel = "Shader";
				
				if (ImGui::BeginCombo("Shader",My_map::GetShaderPaths()[mat->shaderindex].c_str()))
				{
					for (int i = 0; i < (int)My_map::GetShaderPaths().size(); i++)
					{
						bool selected = (i == mat->shaderindex);
						if (ImGui::Selectable(My_map::GetShaderPaths()[i].c_str(), selected))
						{
							mat->shaderindex = i;
							mat->shader = ShaderLibiray::Get(My_map::GetShaderPaths()[i]);
							m_NeedRebuild = true;
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				// Rebuild widgets each frame for fresh pointers
				BuildMaterialWidgets(mat);

				for (auto* widget : m_MaterialWidgets)
					widget->_Draw();
			}
		}
		else
		{
			ClearMaterialWidgets();
		}

		if (ImGui::Button("Add Component"))
		{
			ImGui::OpenPopup("Components");
		}
		if (ImGui::BeginPopup("Components"))
		{
			for (auto name : componentname)
			{
				if (ImGui::MenuItem(name.c_str()))
				{
					if (name == "MeshRender")
					{
						if (!entity.HasComponent<MeshRender>())
						{
							auto& mr = entity.AddComponent<MeshRender>();
							mr.materials.push_back(CreateRef<Material>("D:/Code/C++/Tsundere/res/shaders/Lit.shader"));
							debuglog("AddComponent: MeshRender with default material")
						}
					}
					else if (name == "Material")
					{
						if (entity.HasComponent<MeshRender>())
						{
							auto& mr = entity.GetComponent<MeshRender>();
							mr.materials.push_back(CreateRef<Material>("res/shaders/Lit.shader"));
							debuglog("AddComponent: Material added to MeshRender")
						}
						else
						{
							debugwarring("Add MeshRender first before adding Material")
						}
					}
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::End();
		}
	}

	ImGui::End();
}

void Inspect::OnEvent(Eventing::Event<>& event)
{
}
