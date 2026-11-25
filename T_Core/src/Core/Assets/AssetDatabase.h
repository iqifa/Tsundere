#pragma once

#include "Core/Core.h"
#include "Core/Assets/ImportSettings.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

enum class AssetKind : uint8_t
{
	Model,
	Texture,
	Material,
	Shader,
	Scene,
	Cubemap
};

struct AssetRecord
{
	uint64_t uid = 0;
	AssetKind kind = AssetKind::Model;
	std::string path;
};

// Registry of files under Assets/. Each file keeps a stable uid in a sidecar
// "<filename>.meta". Sync discovers files and writes a uid for any that lack one.
// GPU resources stay unloaded until a picker selects a record.
class T_API AssetDatabase
{
public:
	static void Sync(const std::filesystem::path& root);

	static std::vector<AssetRecord> GetByKind(AssetKind kind);
	static uint64_t UidForPath(const std::filesystem::path& path, AssetKind kind);
	static std::string Resolve(uint64_t uid, AssetKind kind);

	static std::string NormalizePath(const std::filesystem::path& path);

	static std::vector<uint32_t> DisabledSubmeshes(const std::filesystem::path& model);
	static bool SetSubmeshActive(const std::filesystem::path& model, uint32_t index,
		bool active, std::string& error);

	// Import settings live in the asset's .meta. Getters return the defaults
	// when the file has no .meta or no import section. Setters write the .meta
	// (creating it if needed) and mark the settings confirmed; the caller then
	// requests a reload so the new settings take effect.
	static TextureImportSettings GetTextureImportSettings(const std::filesystem::path& texture);
	static bool SetTextureImportSettings(const std::filesystem::path& texture,
		const TextureImportSettings& settings, std::string& error);
	static ModelImportSettings GetModelImportSettings(const std::filesystem::path& model);
	static bool SetModelImportSettings(const std::filesystem::path& model,
		const ModelImportSettings& settings, std::string& error);

	// True for textures and models under the synced root whose import settings
	// were written by Sync and never applied from the editor. In-memory lookup.
	static bool NeedsImportReview(const std::filesystem::path& path);

	// Per-asset counter bumped when an editor rewrites an asset's content.
	// Consumers remember the value they loaded and reload when it differs.
	// Starts at 0; uid 0 is ignored.
	static void MarkContentChanged(uint64_t uid);
	static uint64_t ContentRevision(uint64_t uid);
};

} // namespace Engine
