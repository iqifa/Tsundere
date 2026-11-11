#pragma once

#include "Core/Assets/AssetDatabase.h"
#include "Core/Layer/Layer.h"
#include "Core/Material.h"
#include "Core/MaterialLibrary.h"
#include "Platform/RHI/RHITextureLibrary.h"
#include "Platform/FileDialog.h"
#include "HeadLine.h"
#include <filesystem>
#include <vector>
#include <string>
#include <functional>
#include <array>

namespace fs = std::filesystem;

class T_API AssetsPanel : public Engine::Layer
{
public:
	AssetsPanel(const std::string& name = "Assets");
	~AssetsPanel() override = default;

	void OnImGuiRender() override;

	std::function<void(const std::string&)> OnOpenScene;
	const fs::path& GetRootDirectory() const { return m_RootDirectory; }
	void Refresh() { RefreshCurrentDirectory(); SyncRegistry(); }

	// Set the root directory to browse (default: "Assets/")
	void SetRootDirectory(const std::string& path);

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
			Scene,
			Cubemap,
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
	void SyncRegistry();
	void NavigateToDirectory(const fs::path& directory);

	void ImportAsset();
	void DrawCubemapImport();
	std::array<std::string, 6> m_CubemapFaces;
	char m_CubemapName[128] = "Skybox";
	std::string m_CubemapError;
	void CreateMaterialAsset();
	void DeleteAsset(const std::string& path);
	void RenameAsset(const std::string& oldPath, const std::string& newName);
	void OpenAsset(const AssetEntry& entry);

	fs::path m_RootDirectory;
	fs::path m_CurrentDirectory;
	std::vector<AssetEntry> m_CurrentAssets;

	float m_ThumbnailSize = 80.0f;
	int m_SelectedIndex = -1;

	bool m_ShowHiddenFiles = false;
};
