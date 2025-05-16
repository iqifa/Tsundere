#include "Inspect.h"

Inspect::Inspect(Ref<Scene> scene, std::string name):BasePanel(name)
{
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
				
			}
		}
		ImGui::End();
	}

	ImGui::End();
}

void Inspect::OnEvent(Eventing::Event<>& event)
{
}
