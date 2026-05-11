#include "Inspect.h"

Inspect::Inspect(Ref<Scene> scene, std::string name) : BasePanel(name)
{
	m_Context = scene;
}

void Inspect::OnUpdate()
{
}

void Inspect::OnImGuiRender()
{
	ImGui::Begin(m_HeadTitle.c_str());

	if (m_SelectedContext != null)
	{
		Entity entity{ m_Context.get(), m_SelectedContext };

		DiscoverAndRenderComponents(entity);
		RenderMaterialInspector(entity);

		if (ImGui::Button("Add Component"))
			ImGui::OpenPopup("AddComponentPopup");

		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			for (const auto& name : GetAddableComponentNames())
			{
				if (ImGui::MenuItem(name.c_str()))
				{
					DispatchAddComponent(name, entity);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}
	}

	ImGui::End();
}

void Inspect::OnEvent(Eventing::Event<>& event)
{
}
