#pragma once

#include "Core/Core.h"
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
};

} // namespace Engine
