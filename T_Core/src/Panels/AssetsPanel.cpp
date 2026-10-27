#include "AssetsPanel.h"
#include "Core/Application.h"
#include "Core/Threading/ResourceLoader.h"
#include "Core/Log.h"
#include "Panels/MeshFilePath.h"
#include <algorithm>

AssetsPanel::AssetsPanel(const std::string& name)
	: Layer(name)
	, m_RootDirectory("res")
	, m_CurrentDirectory("res")
{
	if (!fs::exists(m_RootDirectory))
	{
		LOG_WARN("AssetsPanel", "Root directory does not exist: {0}", m_RootDirectory.string());
		m_RootDirectory = fs::current_path();
		m_CurrentDirectory = m_RootDirectory;
	}
	RefreshCurrentDirectory();
}

void AssetsPanel::SetRootDirectory(const std::string& path)
{
	m_RootDirectory = path;
	m_CurrentDirectory = m_RootDirectory;
	RefreshCurrentDirectory();
}

void AssetsPanel::OnImGuiRender()
{
	ImGui::Begin("Assets");

	// Toolbar
	if (ImGui::Button("Import"))
		ImportAsset();

	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
		RefreshCurrentDirectory();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::SliderFloat("Size", &m_ThumbnailSize, 50.0f, 150.0f, "%.0f");

	ImGui::Separator();

	// Split view: left = directory tree, right = asset grid
	ImGui::BeginChild("##dirleft", ImVec2(200, 0), true);
	{
		ImGui::TextUnformatted("Folders");
		ImGui::Separator();

		bool rootOpen = true;
		DrawDirectoryTree(m_RootDirectory, rootOpen);
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("##assetright", ImVec2(0, 0), true);
	{
		// Breadcrumb navigation
		std::string relativePath = fs::relative(m_CurrentDirectory, m_RootDirectory).string();
		if (relativePath.empty() || relativePath == ".")
			relativePath = "Root";
		ImGui::Text("Path: %s", relativePath.c_str());

		if (m_CurrentDirectory != m_RootDirectory)
		{
			ImGui::SameLine();
			if (ImGui::Button("< Back"))
				NavigateToDirectory(m_CurrentDirectory.parent_path());
		}

		ImGui::Separator();
		DrawAssetGrid();
	}
	ImGui::EndChild();

	ImGui::End();
}

void AssetsPanel::DrawDirectoryTree(const fs::path& directory, bool& nodeOpen)
{
	if (!fs::exists(directory) || !fs::is_directory(directory))
		return;

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

	bool isCurrentDir = (directory == m_CurrentDirectory);
	if (isCurrentDir)
		flags |= ImGuiTreeNodeFlags_Selected;

	// Check if directory has subdirectories
	bool hasSubdirs = false;
	for (const auto& entry : fs::directory_iterator(directory))
	{
		if (entry.is_directory())
		{
			hasSubdirs = true;
			break;
		}
	}

	if (!hasSubdirs)
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	std::string label = directory.filename().string();
	if (label.empty())
		label = directory.string();

	nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);

	if (ImGui::IsItemClicked())
		NavigateToDirectory(directory);

	if (nodeOpen && hasSubdirs)
	{
		// Recursively draw subdirectories
		std::vector<fs::path> subdirs;
		for (const auto& entry : fs::directory_iterator(directory))
		{
			if (entry.is_directory())
				subdirs.push_back(entry.path());
		}

		std::sort(subdirs.begin(), subdirs.end());

		for (const auto& subdir : subdirs)
		{
			bool subOpen = false;
			DrawDirectoryTree(subdir, subOpen);
		}

		ImGui::TreePop();
	}
}

void AssetsPanel::DrawAssetGrid()
{
	const float cellSize = m_ThumbnailSize + 20.0f;
	const float panelWidth = ImGui::GetContentRegionAvail().x;
	int columnCount = std::max(1, (int)(panelWidth / cellSize));

	ImGui::Columns(columnCount, nullptr, false);

	for (size_t i = 0; i < m_CurrentAssets.size(); ++i)
	{
		const auto& asset = m_CurrentAssets[i];
		ImGui::PushID((int)i);

		bool isSelected = (m_SelectedIndex == (int)i);

		// Icon + label
		ImVec2 cursorPos = ImGui::GetCursorPos();

		if (ImGui::Selectable("##assetbg", isSelected, 0, ImVec2(m_ThumbnailSize, m_ThumbnailSize + 30)))
		{
			m_SelectedIndex = (int)i;
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			OpenAsset(asset);
		}

		DrawAssetContextMenu(asset);

		ImGui::SetCursorPos(cursorPos);

		// Draw icon placeholder
		ImGui::BeginGroup();
		{
			std::string icon = GetAssetTypeIcon(asset.type);

			// Center icon text
			ImVec2 iconSize = ImGui::CalcTextSize(icon.c_str());
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (m_ThumbnailSize - iconSize.x) * 0.5f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (m_ThumbnailSize - iconSize.y) * 0.5f);

			if (asset.type == AssetEntry::Type::Texture)
			{
				// Try to display texture
				Ref<RHITexture2D> tex = TextureLibiary::Get(asset.path);
				if (tex)
				{
					ImTextureID texID = RHIImGUIRenderer::GetTextureID(tex.get());
					if (texID != ImTextureID_Invalid)
					{
						ImGui::SetCursorPos(cursorPos);
						ImGui::Image(texID, ImVec2(m_ThumbnailSize, m_ThumbnailSize), ImVec2(0, 1), ImVec2(1, 0));
					}
					else
					{
						ImGui::Text("%s", icon.c_str());
					}
				}
				else
				{
					ImGui::Text("%s", icon.c_str());
				}
			}
			else
			{
				ImGui::Text("%s", icon.c_str());
			}

			// Asset name (truncated if too long)
			ImGui::SetCursorPosX(cursorPos.x);
			std::string displayName = asset.name;
			if (displayName.length() > 12)
				displayName = displayName.substr(0, 9) + "...";

			ImVec2 nameSize = ImGui::CalcTextSize(displayName.c_str());
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (m_ThumbnailSize - nameSize.x) * 0.5f);
			ImGui::TextUnformatted(displayName.c_str());

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(asset.name.c_str());
				ImGui::TextUnformatted(asset.path.c_str());
				ImGui::EndTooltip();
			}
		}
		ImGui::EndGroup();

		ImGui::PopID();
		ImGui::NextColumn();
	}

	ImGui::Columns(1);
}

void AssetsPanel::DrawAssetContextMenu(const AssetEntry& entry)
{
	if (ImGui::BeginPopupContextItem("##assetcontext"))
	{
		if (ImGui::MenuItem("Open"))
			OpenAsset(entry);

		ImGui::Separator();

		if (ImGui::MenuItem("Delete"))
			DeleteAsset(entry.path);

		if (ImGui::MenuItem("Rename"))
		{
			// TODO: Implement rename dialog
			LOG_INFO("AssetsPanel", "Rename not yet implemented");
		}

		if (ImGui::MenuItem("Show in Explorer"))
		{
			std::string command = "explorer /select,\"" + fs::absolute(entry.path).string() + "\"";
			system(command.c_str());
		}

		ImGui::EndPopup();
	}
}

AssetsPanel::AssetEntry::Type AssetsPanel::GetAssetType(const fs::path& path)
{
	if (fs::is_directory(path))
		return AssetEntry::Type::Directory;

	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
		return AssetEntry::Type::Model;
	if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr")
		return AssetEntry::Type::Texture;
	if (ext == ".tmat")
		return AssetEntry::Type::Material;
	if (ext == ".shader" || ext == ".glsl" || ext == ".vert" || ext == ".frag")
		return AssetEntry::Type::Shader;

	return AssetEntry::Type::Unknown;
}

std::string AssetsPanel::GetAssetTypeIcon(AssetEntry::Type type)
{
	switch (type)
	{
	case AssetEntry::Type::Directory: return "[DIR]";
	case AssetEntry::Type::Model: return "[3D]";
	case AssetEntry::Type::Texture: return "[IMG]";
	case AssetEntry::Type::Material: return "[MAT]";
	case AssetEntry::Type::Shader: return "[SHD]";
	case AssetEntry::Type::Unknown: return "[?]";
	default: return "[?]";
	}
}

void AssetsPanel::RefreshCurrentDirectory()
{
	m_CurrentAssets.clear();

	if (!fs::exists(m_CurrentDirectory) || !fs::is_directory(m_CurrentDirectory))
		return;

	for (const auto& entry : fs::directory_iterator(m_CurrentDirectory))
	{
		if (!m_ShowHiddenFiles && entry.path().filename().string()[0] == '.')
			continue;

		AssetEntry::Type type = GetAssetType(entry.path());
		bool isDir = fs::is_directory(entry.path());

		m_CurrentAssets.emplace_back(
			entry.path().filename().string(),
			entry.path().string(),
			type,
			isDir
		);
	}

	// Sort: directories first, then by name
	std::sort(m_CurrentAssets.begin(), m_CurrentAssets.end(),
		[](const AssetEntry& a, const AssetEntry& b)
		{
			if (a.isDirectory != b.isDirectory)
				return a.isDirectory > b.isDirectory;
			return a.name < b.name;
		});

	m_SelectedIndex = -1;
}

void AssetsPanel::NavigateToDirectory(const fs::path& directory)
{
	if (fs::exists(directory) && fs::is_directory(directory))
	{
		m_CurrentDirectory = directory;
		RefreshCurrentDirectory();
	}
}

void AssetsPanel::ImportAsset()
{
	std::string path = OpenFileDialog("");
	if (path.empty())
		return;

	// Copy file to current directory
	fs::path sourcePath(path);
	fs::path destPath = m_CurrentDirectory / sourcePath.filename();

	try
	{
		fs::copy_file(sourcePath, destPath, fs::copy_options::overwrite_existing);
		LOG_INFO("AssetsPanel", "Imported asset: {0}", destPath.string());

		// Load asset based on type
		AssetEntry::Type type = GetAssetType(destPath);
		if (type == AssetEntry::Type::Texture)
			Engine::ResourceLoader::RequestTextureLoad(destPath.string());
		else if (type == AssetEntry::Type::Model)
			Engine::ResourceLoader::RequestModelLoad(destPath.string());

		RefreshCurrentDirectory();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("AssetsPanel", "Failed to import asset: {0}", e.what());
	}
}

void AssetsPanel::DeleteAsset(const std::string& path)
{
	try
	{
		if (fs::is_directory(path))
			fs::remove_all(path);
		else
			fs::remove(path);

		LOG_INFO("AssetsPanel", "Deleted asset: {0}", path);
		RefreshCurrentDirectory();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("AssetsPanel", "Failed to delete asset: {0}", e.what());
	}
}

void AssetsPanel::RenameAsset(const std::string& oldPath, const std::string& newName)
{
	try
	{
		fs::path old(oldPath);
		fs::path newPath = old.parent_path() / newName;
		fs::rename(old, newPath);

		LOG_INFO("AssetsPanel", "Renamed asset: {0} -> {1}", oldPath, newPath.string());
		RefreshCurrentDirectory();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("AssetsPanel", "Failed to rename asset: {0}", e.what());
	}
}

void AssetsPanel::OpenAsset(const AssetEntry& entry)
{
	switch (entry.type)
	{
	case AssetEntry::Type::Directory:
		NavigateToDirectory(entry.path);
		break;

	case AssetEntry::Type::Model:
		if (s_Scene)
			ModelEditorPanel::Open(entry.path);
		break;

	case AssetEntry::Type::Texture:
		// Texture preview (could open a separate panel in the future)
		LOG_INFO("AssetsPanel", "Opened texture: {0}", entry.path);
		break;

	case AssetEntry::Type::Material:
		// TODO: Open MaterialEditorPanel when implemented
		LOG_INFO("AssetsPanel", "Opened material: {0}", entry.path);
		break;

	case AssetEntry::Type::Shader:
		// Open shader in external editor
		{
			std::string command = "start \"\" \"" + entry.path + "\"";
			system(command.c_str());
		}
		break;

	default:
		LOG_WARN("AssetsPanel", "Unknown asset type: {0}", entry.path);
		break;
	}
}
