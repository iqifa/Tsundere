#include "Inspect.h"

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

void Inspect::OnImGuiRender()
{
	ImGui::Begin(m_HeadTitle.c_str());



	if (m_SelectedContext != null)
	{
		Entity entity{ m_Context.get(),m_SelectedContext };



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
					debuglog("AddComponent: " + name)
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
