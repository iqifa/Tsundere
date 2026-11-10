#include "Inspect.h"
#include "Platform/FileDialog.h"
#include "Scene/SceneSerializer.h"

Inspect::Inspect(Ref<Scene> scene, std::string name) : BasePanel(name)
{
	m_Context = scene;
}

void Inspect::RememberSceneAsset()
{
 m_SceneAssetUid = 0;
 if (!m_Assets) return;
 m_Assets->Refresh();
 for (const auto& asset : Engine::AssetDatabase::GetByKind(Engine::AssetKind::Scene))
  if (asset.path == Engine::AssetDatabase::NormalizePath(m_SceneFile))
   m_SceneAssetUid = asset.uid;
}

void Inspect::LoadScene(const std::string& path)
{
 if (m_Assets) Engine::AssetDatabase::Sync(m_Assets->GetRootDirectory());
 if (path.empty() || !SceneSerializer::Load(*m_Context, path, m_SceneFileError)) return;
 m_SelectedContext = null;
 m_SceneFile = path;
 RememberSceneAsset();
}

void Inspect::SaveScene(bool saveAs)
{
 // Follow the stable asset UID when the scene or its directory is renamed.
 if (m_SceneAssetUid && m_Assets)
 {
  Engine::AssetDatabase::Sync(m_Assets->GetRootDirectory());
  bool found = false;
  for (const auto& asset : Engine::AssetDatabase::GetByKind(Engine::AssetKind::Scene))
   if (asset.uid == m_SceneAssetUid) { m_SceneFile = asset.path; found = true; break; }
  if (!found) saveAs = true;
 }
 std::string path = m_SceneFile;
 if (saveAs || path.empty())
 {
  const std::string directory = m_Assets ? m_Assets->GetRootDirectory().string() : "";
  path = SaveFileDialog("Tsundere Scene\0*.tscene\0", "tscene", directory.c_str());
 }
 if (path.empty() || !SceneSerializer::Save(*m_Context, path, m_SceneFileError)) return;
 m_SceneFile = path;
 RememberSceneAsset();
}

void Inspect::OnUpdate()
{
}

void Inspect::OnImGuiRender()
{
	ImGui::Begin(m_HeadTitle.c_str());

	if (m_Context->m_Registry.valid(m_SelectedContext))
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

	ImGui::Begin("Hire");
	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		SaveScene(ImGui::GetIO().KeyShift);
	if (ImGui::Button("Save Scene")) SaveScene();
	ImGui::SameLine();
	if (ImGui::Button("Save As...")) SaveScene(true);
	ImGui::SameLine();
	if (ImGui::Button("Load Scene"))
		ImGui::OpenPopup("Load scene?");
	if (ImGui::BeginPopupModal("Load scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Loading replaces the current scene. Save any changes first.");
		if (ImGui::Button("Load"))
		{
			const auto path = OpenFileDialog("Tsundere Scene\0*.tscene\0");
			LoadScene(path);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	if (!m_SceneFile.empty()) ImGui::TextWrapped("%s", m_SceneFile.c_str());
	if (!m_SceneFileError.empty()) ImGui::TextWrapped("%s", m_SceneFileError.c_str());
	ImGui::Separator();

	for (auto entityID : m_Context->m_Registry.view<Top>())
	{
		Entity entity = Entity{ m_Context.get(),entityID };

		DrawEntityNode(entity);
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
		!ImGui::IsAnyItemHovered())
		m_SelectedContext = null;

	if (ImGui::BeginPopupContextWindow(0, 1))
	{
		if (ImGui::MenuItem("Create Empty Entity"))
		{
			//Entity entity = m_Context->CreateEntity("Empty Entity");
			//Entity childOne = m_Context->CreateEntity("ChildOne");

			//entity.addchildwithchangeparent(childOne);

			Entity entity = m_Context->CreateEntity("Empty Entity");
			if (m_SelectedContext != null)
			{
				Entity{ m_Context.get(),m_SelectedContext }.addchildwithchangeparent(entity);
			}

		}
		if (m_SelectedContext != null) {
			if (ImGui::MenuItem("Delete"))
			{
				for (auto entityID : m_Context->m_Registry.view<ID>())
				{
					Info_Core((int)entityID);
				}
				m_Context->DestoryEntity(Entity{ m_Context.get(),m_SelectedContext });
				m_SelectedContext = null;
				BVHBuilder::MarkActiveDirty();
			}
		}

		ImGui::EndPopup();
	}
	ImGui::End();
}

void Inspect::OnEvent(Eventing::Event<>& event)
{
}

void Inspect::DrawEntityNode(Entity entity)
{
	if (m_SelectedContext != null)
	{
		bool a = Entity{ m_Context.get(),m_SelectedContext }.HasComponent <Tag>();
	}


	auto& tag = entity.GetComponent<Tag>();
	ImGuiTreeNodeFlags flags = (m_SelectedContext == entity ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
	bool opened = ImGui::TreeNodeEx((void*)(entt::entity)entity, flags, tag.tag.c_str());

	if (ImGui::IsItemClicked())
	{
		m_SelectedContext = entity;
		Info_Core("Switch Select" + Entity{ m_Context.get(),m_SelectedContext }.GetComponent<Tag>().tag);
	}
	bool Deleted = false;
	/*if (ImGui::BeginPopupContextWindow(0, 1))
	{

		if (ImGui::MenuItem("Delete Empty"))
			Deleted = true;
		ImGui::EndPopup();
	}*/
	if (opened)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

		for (auto childID : entity.GetComponent<Child>().children)
		{
			Entity child = Entity{ m_Context.get(),childID };
			DrawEntityNode(child);
		}


		ImGui::TreePop();
	}
	if (Deleted)
	{
		for (auto childID : entity.GetComponent<Child>().children)
		{
			Entity child = Entity{ m_Context.get(),childID };
			m_Context->DestoryEntity(child);
		}
		m_Context->DestoryEntity({ m_Context.get(),m_SelectedContext });
	}
}
