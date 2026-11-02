#include "Panels/MaterialEditorPanel.h"
#include "Scene/Scene.h"
#include "Panels/MaterialInspectorPanel.h"

MaterialEditorPanel*& MaterialEditorPanel::Instance()
{
	static MaterialEditorPanel* instance = nullptr;
	return instance;
}

void MaterialEditorPanel::OnImGuiRender()
{
	if (!m_Open)
	{
		DestroySelf();
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Material Editor", &m_Open))
	{
		ImGui::End();
		return;
	}

	if (m_Material)
		DrawMaterialFields(m_Material);
	else
		ImGui::TextUnformatted("No material");

	ImGui::End();
}
