#include "AssetsPanel.h"
#include "Core/Application.h"
#include "Core/Threading/ResourceLoader.h"
#include "Debug/Debug.h"
#include "Panels/MeshFilePath.h"
#include "Panels/ModelEditorPanel.h"
#include "Panels/MaterialEditorPanel.h"
#include "Panels/TextureViewerPanel.h"
#include <algorithm>
#include <cctype>

// Resolve the project root (the directory containing res/) as an absolute path.
// The working directory varies between VS launch, double-click launch and
// working-tree layouts, so relative asset paths break; anchoring on res/ is
// stable because every shader/material path in the engine is res/-relative.
static fs::path FindProjectRoot()
{
	fs::path dir = fs::current_path();
	for (int depth = 0; depth < 6; ++depth)
	{
		if (fs::exists(dir / "res"))
			return dir;
		fs::path parent = dir.parent_path();
		if (parent == dir)
			break;
		dir = parent;
	}
	return fs::current_path();
}

AssetsPanel::AssetsPanel(const std::string& name)
	: Layer(name)
	, m_RootDirectory(FindProjectRoot() / "Assets")
	, m_CurrentDirectory(m_RootDirectory)
{
	// Create the asset workspace if it does not exist yet, with standard
	// subfolders so imports have an obvious home.
	if (!fs::exists(m_RootDirectory))
	{
		std::error_code ec;
		fs::create_directories(m_RootDirectory, ec);
		for (const char* sub : { "Models", "Textures", "Materials" })
			fs::create_directories(m_RootDirectory / sub, ec);
		if (ec)
			Warn_Core("Could not create Assets directory: {0}", ec.message());
	}
	if (!fs::exists(m_RootDirectory))
	{
		Warn_Core("Root directory does not exist: {0}", m_RootDirectory.string());
		m_RootDirectory = FindProjectRoot();
		m_CurrentDirectory = m_RootDirectory;
	}
	RefreshCurrentDirectory();
	SyncRegistry();
}

void AssetsPanel::SetRootDirectory(const std::string& path)
{
	m_RootDirectory = path;
	m_CurrentDirectory = m_RootDirectory;
	RefreshCurrentDirectory();
	SyncRegistry();
}

void AssetsPanel::OnImGuiRender()
{
	ImGui::Begin("Assets");

	// Toolbar
	if (ImGui::Button("Import"))
		ImportAsset();

	ImGui::SameLine();
	if (ImGui::Button("New Material"))
		CreateMaterialAsset();

	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
	{
		RefreshCurrentDirectory();
		SyncRegistry();
	}

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

		// Blank-area context menu: create new assets here
		if (ImGui::BeginPopupContextWindow("##assetsBlankCtx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("New Material"))
				CreateMaterialAsset();
			if (ImGui::MenuItem("Import Asset..."))
				ImportAsset();
			ImGui::EndPopup();
		}
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
	const float gap = 8.0f;
	const float panelWidth = ImGui::GetContentRegionAvail().x;
	int columnCount = (int)((panelWidth + gap) / (cellSize + gap));
	if (columnCount < 1)
		columnCount = 1;

	const float nameHeight = ImGui::GetTextLineHeight();
	const float cellHeight = m_ThumbnailSize + nameHeight + 12.0f;
	const float pitchX = cellSize + gap;
	const float pitchY = cellHeight + gap;
	const ImVec2 origin = ImGui::GetCursorScreenPos();

	auto ellipsize = [](const std::string& text, float maxWidth)
	{
		if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
			return text;
		const std::string ellipsis = "...";
		const float ellipsisW = ImGui::CalcTextSize(ellipsis.c_str()).x;
		if (ellipsisW >= maxWidth)
			return ellipsis;
		std::string cut = text;
		while (!cut.empty() && ImGui::CalcTextSize(cut.c_str()).x + ellipsisW > maxWidth)
		{
			cut.pop_back();
			while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)
				cut.pop_back();
		}
		return cut + ellipsis;
	};

	for (size_t i = 0; i < m_CurrentAssets.size(); ++i)
	{
		// Copy: opening an entry (e.g. double-click on a directory) refreshes
		// m_CurrentAssets and would leave the loop iterating a dangling reference.
		const AssetEntry asset = m_CurrentAssets[i];
		const int column = (int)i % columnCount;
		const int row = (int)i / columnCount;
		const ImVec2 cellTopLeft(origin.x + column * pitchX, origin.y + row * pitchY);
		const ImVec2 thumbTopLeft(cellTopLeft.x + 10.0f, cellTopLeft.y + 4.0f);

		ImGui::PushID((int)i);
		ImGui::SetCursorScreenPos(cellTopLeft);
		ImGui::InvisibleButton("##assetbg", ImVec2(cellSize, cellHeight));
		const bool hovered = ImGui::IsItemHovered();
		const bool isSelected = (m_SelectedIndex == (int)i);
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			m_SelectedIndex = (int)i;
			OpenAsset(asset);
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			m_SelectedIndex = (int)i;

		DrawAssetContextMenu(asset);

		if (hovered)
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(asset.name.c_str());
			ImGui::TextUnformatted(asset.path.c_str());
			ImGui::EndTooltip();
		}

		if (isSelected || hovered)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			ImU32 bg = ImGui::GetColorU32(
				isSelected ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered);
			dl->AddRectFilled(cellTopLeft,
				ImVec2(cellTopLeft.x + cellSize, cellTopLeft.y + cellHeight),
				bg, 4.0f);
		}

		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			bool drewThumb = false;

			if (asset.type == AssetEntry::Type::Texture)
			{
				Ref<RHITexture2D> tex = TextureLibiary::Get(asset.path);
				if (tex)
				{
					ImTextureID texID = RHIImGUIRenderer::GetTextureID(tex.get());
					if (texID != ImTextureID_Invalid)
					{
						dl->AddImageRounded(texID, thumbTopLeft,
							ImVec2(thumbTopLeft.x + m_ThumbnailSize, thumbTopLeft.y + m_ThumbnailSize),
							ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 4.0f);
						drewThumb = true;
					}
				}
			}

			if (!drewThumb)
			{
				ImU32 tile = IM_COL32(60, 60, 68, 200);
				ImU32 fg = IM_COL32(220, 220, 225, 255);
				switch (asset.type)
				{
				case AssetEntry::Type::Directory: tile = IM_COL32(70, 100, 150, 200); break;
				case AssetEntry::Type::Model:     tile = IM_COL32(150, 110, 60, 200); break;
				case AssetEntry::Type::Texture:   tile = IM_COL32(90, 130, 90, 200); break;
				case AssetEntry::Type::Material:  tile = IM_COL32(140, 80, 140, 200); break;
				case AssetEntry::Type::Shader:    tile = IM_COL32(80, 120, 130, 200); break;
				default: break;
				}
				dl->AddRectFilled(thumbTopLeft,
					ImVec2(thumbTopLeft.x + m_ThumbnailSize, thumbTopLeft.y + m_ThumbnailSize),
					tile, 4.0f);

				std::string icon = GetAssetTypeIcon(asset.type);
				ImVec2 iconSize = ImGui::CalcTextSize(icon.c_str());
				dl->AddText(ImVec2(thumbTopLeft.x + (m_ThumbnailSize - iconSize.x) * 0.5f,
					thumbTopLeft.y + (m_ThumbnailSize - iconSize.y) * 0.5f),
					fg, icon.c_str());
			}

			const float nameMaxWidth = cellSize - 8.0f;
			const std::string displayName = ellipsize(asset.name, nameMaxWidth);
			const ImVec2 nameSize = ImGui::CalcTextSize(displayName.c_str());
			dl->AddText(
				ImVec2(cellTopLeft.x + (cellSize - nameSize.x) * 0.5f,
					thumbTopLeft.y + m_ThumbnailSize + 4.0f),
				IM_COL32(220, 220, 225, 255),
				displayName.c_str());
		}

		ImGui::PopID();
	}

	const int rows = m_CurrentAssets.empty()
		? 0
		: ((int)m_CurrentAssets.size() + columnCount - 1) / columnCount;
	ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * pitchY));
	ImGui::Dummy(ImVec2(0.0f, 0.0f));
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
			Info_Core("Rename not yet implemented");
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

	if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".pmx" || ext == ".pmd")
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

void AssetsPanel::SyncRegistry()
{
	Engine::AssetDatabase::Sync(m_RootDirectory);
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

		std::string ext = entry.path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (ext == ".meta")
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
	// One dialog accepting all supported asset types
	std::string path = OpenFileDialog(
		"Supported Assets\0*.obj;*.fbx;*.gltf;*.glb;*.pmx;*.pmd;*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.tmat\0"
		"Model Files\0*.obj;*.fbx;*.gltf;*.glb;*.pmx;*.pmd\0"
		"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr\0"
		"Material Files\0*.tmat\0"
		"All Files\0*.*\0");
	if (path.empty())
		return;

	// Copy file to current directory
	fs::path sourcePath(path);
	fs::path destPath = m_CurrentDirectory / sourcePath.filename();

	try
	{
		// Destination folder may have been deleted while browsing
		std::error_code ec;
		fs::create_directories(m_CurrentDirectory, ec);
		fs::copy_file(sourcePath, destPath, fs::copy_options::overwrite_existing);
		Info_Core("Imported asset: {0}", destPath.string());

		// Load asset based on type
		AssetEntry::Type type = GetAssetType(destPath);
		if (type == AssetEntry::Type::Texture)
			Engine::ResourceLoader::RequestTextureLoad(destPath.string());
		else if (type == AssetEntry::Type::Model)
		{
			My_map::AddModelPath(destPath.string());
			Engine::ResourceLoader::RequestModelLoad(destPath.string());
			ModelEditorPanel::Open(destPath.string());
		}
		else if (type == AssetEntry::Type::Material)
		{
			// Preload into the material library so it is ready to assign
			if (Engine::MaterialLibrary::Load(destPath.string()))
				Info_Core("Loaded material asset: {0}", destPath.string());
		}

		RefreshCurrentDirectory();
		SyncRegistry();
	}
	catch (const std::exception& e)
	{
		Error_Core("Failed to import asset: {0}", e.what());
	}
}

void AssetsPanel::CreateMaterialAsset()
{
	// Make sure the target directory exists before writing into it
	std::error_code ec;
	fs::create_directories(m_CurrentDirectory, ec);

	// Find a non-clashing name: NewMaterial.tmat, NewMaterial_1.tmat, ...
	fs::path base = m_CurrentDirectory / "NewMaterial";
	fs::path target = base.string() + ".tmat";
	int suffix = 0;
	while (fs::exists(target))
		target = base.string() + "_" + std::to_string(++suffix) + ".tmat";

	// Create with a shader that actually exists so the inspector has
	// parameters to show (Lamber.shader ships with the engine)
	Ref<RHIShader> shader = ShaderLibiray::Get("res/shaders/Lamber.shader");
	Ref<Engine::Material> mat = Engine::MaterialLibrary::CreateDefault(
		target.filename().string(), shader);

	if (!Engine::MaterialLibrary::Save(mat, target.string()))
	{
		Error_Core("Failed to create material: {0}", target.string());
		return;
	}

	Info_Core("Created material: {0}", target.string());
	RefreshCurrentDirectory();
	SyncRegistry();
}

void AssetsPanel::DeleteAsset(const std::string& path)
{
	try
	{
		if (fs::is_directory(path))
			fs::remove_all(path);
		else
		{
			fs::path meta(fs::path(path).string() + ".meta");
			std::error_code ec;
			fs::remove(meta, ec);
			fs::remove(path);
		}

		Info_Core("Deleted asset: {0}", path);
		RefreshCurrentDirectory();
		SyncRegistry();
	}
	catch (const std::exception& e)
	{
		Error_Core("Failed to delete asset: {0}", e.what());
	}
}

void AssetsPanel::RenameAsset(const std::string& oldPath, const std::string& newName)
{
	try
	{
		fs::path old(oldPath);
		fs::path newPath = old.parent_path() / newName;
		fs::rename(old, newPath);

		fs::path oldMeta(old.string() + ".meta");
		fs::path newMeta(newPath.string() + ".meta");
		if (fs::exists(oldMeta))
		{
			std::error_code ec;
			if (fs::exists(newMeta))
				fs::remove(newMeta, ec);
			fs::rename(oldMeta, newMeta);
		}

		Info_Core("Renamed asset: {0} -> {1}", oldPath, newPath.string());
		RefreshCurrentDirectory();
		SyncRegistry();
	}
	catch (const std::exception& e)
	{
		Error_Core("Failed to rename asset: {0}", e.what());
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
		ModelEditorPanel::Open(entry.path);
		break;

	case AssetEntry::Type::Texture:
		TextureViewerPanel::Open(entry.path);
		break;

	case AssetEntry::Type::Material:
		MaterialEditorPanel::Open(entry.path);
		break;

	case AssetEntry::Type::Shader:
		// Open shader in external editor
		{
			std::string command = "start \"\" \"" + entry.path + "\"";
			system(command.c_str());
		}
		break;

	default:
		Warn_Core("Unknown asset type: {0}", entry.path);
		break;
	}
}
