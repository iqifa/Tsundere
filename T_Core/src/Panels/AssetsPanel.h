#pragma once

#include "Core/Layer/Layer.h"
#include "Core/Material.h"
#include "Core/MaterialLibrary.h"
#include "Platform/RHI/RHITextureLibrary.h"
#include "Platform/FileDialog.h"
#include "Panels/ModelEditorPanel.h"
#include "HeadLine.h"
#include <filesystem>
#include <vector>
#include <string>

namespace fs = std::filesystem;

class AssetsPanel : public Engine::Layer
{
public:
	AssetsPanel(const std::string& name = "Assets");
	~AssetsPanel() override = default;

	void OnImGuiRender() override;

	// Set the root directory to browse (default: "res/")
	void SetRootDirectory(const std::string& path);

	// Bind scene for Model Editor Panel
	static void BindScene(Ref<Scene> scene) { s_Scene = scene; }

private:
	struct AssetEntry
	{
		enum class Type
		{
			Directory,
			Model,
			Texture,
			Material,
			Shader,
			Unknown
		};

		std::string name;
		std::string path;
		Type type;
		bool isDirectory;

		AssetEntry(const std::string& n, const std::string& p, Type t, bool isDir = false)
			: name(n), path(p), type(t), isDirectory(isDir) {}
	};

	void DrawDirectoryTree(const fs::path& directory, bool& nodeOpen);
	void DrawAssetGrid();
	void DrawAssetContextMenu(const AssetEntry& entry);

	AssetEntry::Type GetAssetType(const fs::path& path);
	std::string GetAssetTypeIcon(AssetEntry::Type type);

	void RefreshCurrentDirectory();
	void NavigateToDirectory(const fs::path& directory);

	void ImportAsset();
	void DeleteAsset(const std::string& path);
	void RenameAsset(const std::string& oldPath, const std::string& newName);
	void OpenAsset(const AssetEntry& entry);

	fs::path m_RootDirectory;
	fs::path m_CurrentDirectory;
	std::vector<AssetEntry> m_CurrentAssets;

	float m_ThumbnailSize = 80.0f;
	int m_SelectedIndex = -1;

	bool m_ShowHiddenFiles = false;

	static inline Ref<Scene> s_Scene;
};
