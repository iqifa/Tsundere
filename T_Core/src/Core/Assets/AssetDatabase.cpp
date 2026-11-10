#include "Core/Assets/AssetDatabase.h"
#include "Debug/Debug.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace Engine {

namespace {

std::unordered_map<uint64_t, AssetRecord> s_Records;
std::shared_mutex s_Mutex;

std::string Trim(std::string text)
{
	const auto begin = text.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos)
		return {};
	const auto end = text.find_last_not_of(" \t\r\n");
	return text.substr(begin, end - begin + 1);
}

std::string LowerExtension(const std::filesystem::path& path)
{
	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext;
}

std::optional<AssetKind> Classify(const std::filesystem::path& path)
{
	const std::string ext = LowerExtension(path);
	if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".pmx" || ext == ".pmd")
		return AssetKind::Model;
	if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr")
		return AssetKind::Texture;
	if (ext == ".tscene") return AssetKind::Scene;
	if (ext == ".tmat")
		return AssetKind::Material;
	if (ext == ".shader" || ext == ".glsl" || ext == ".vert" || ext == ".frag")
		return AssetKind::Shader;
	return std::nullopt;
}

const char* KindToString(AssetKind kind)
{
	switch (kind)
	{
	case AssetKind::Model: return "Model";
	case AssetKind::Texture: return "Texture";
	case AssetKind::Material: return "Material";
	case AssetKind::Shader: return "Shader";
	case AssetKind::Scene: return "Scene";
	}
	return "Model";
}

bool KindFromString(const std::string& text, AssetKind& kind)
{
	if (text == "Scene") { kind = AssetKind::Scene; return true; }
	if (text == "Model") { kind = AssetKind::Model; return true; }
	if (text == "Texture") { kind = AssetKind::Texture; return true; }
	if (text == "Material") { kind = AssetKind::Material; return true; }
	if (text == "Shader") { kind = AssetKind::Shader; return true; }
	return false;
}

std::filesystem::path MetaPathFor(const std::filesystem::path& assetPath)
{
	return std::filesystem::path(assetPath.string() + ".meta");
}

struct ParsedMeta
{
	uint64_t uid = 0;
	std::optional<AssetKind> kind;
};

ParsedMeta ReadMeta(const std::filesystem::path& metaPath)
{
	ParsedMeta meta;
	std::ifstream file(metaPath);
	if (!file.is_open())
		return meta;

	std::string line;
	while (std::getline(file, line))
	{
		line = Trim(line);
		if (line.rfind("uid:", 0) == 0)
		{
			const std::string value = Trim(line.substr(4));
			char* end = nullptr;
			const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
			if (end != value.c_str() && parsed != 0)
				meta.uid = static_cast<uint64_t>(parsed);
		}
		else if (line.rfind("type:", 0) == 0)
		{
			AssetKind kind;
			if (KindFromString(Trim(line.substr(5)), kind))
				meta.kind = kind;
		}
	}
	return meta;
}

bool WriteMeta(const std::filesystem::path& metaPath, uint64_t uid, AssetKind kind)
{
	std::ofstream file(metaPath, std::ios::trunc);
	if (!file.is_open())
		return false;
	file << "uid: " << uid << "\n";
	file << "type: " << KindToString(kind) << "\n";
	return static_cast<bool>(file);
}

uint64_t GenerateUid(const std::unordered_set<uint64_t>& used)
{
	static thread_local std::mt19937_64 rng{ std::random_device{}() };
	std::uniform_int_distribution<uint64_t> dist;
	for (;;)
	{
		const uint64_t id = dist(rng);
		if (id != 0 && used.find(id) == used.end())
			return id;
	}
}

void RemoveOrphanMeta(const std::filesystem::path& metaPath)
{
	const std::string text = metaPath.string();
	constexpr const char* suffix = ".meta";
	constexpr size_t suffixLen = 5;
	if (text.size() <= suffixLen)
		return;

	std::string tail = text.substr(text.size() - suffixLen);
	std::transform(tail.begin(), tail.end(), tail.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (tail != suffix)
		return;

	const std::filesystem::path assetPath(text.substr(0, text.size() - suffixLen));
	std::error_code ec;
	if (std::filesystem::exists(assetPath, ec) || ec)
		return;

	std::filesystem::remove(metaPath, ec);
	if (ec)
		Warn_Core("Failed to remove orphan asset meta {0}: {1}", metaPath.string(), ec.message());
}

void ConsiderFile(
	const std::filesystem::path& file,
	std::unordered_map<uint64_t, AssetRecord>& records,
	std::unordered_set<uint64_t>& used)
{
	const std::string ext = LowerExtension(file);
	if (ext == ".meta")
	{
		RemoveOrphanMeta(file);
		return;
	}

	const std::optional<AssetKind> kind = Classify(file);
	if (!kind)
		return;

	const std::filesystem::path metaPath = MetaPathFor(file);
	const ParsedMeta meta = ReadMeta(metaPath);
	uint64_t uid = meta.uid;
	bool rewrite = false;
	if (uid == 0 || used.find(uid) != used.end())
	{
		if (uid != 0)
			Warn_Core("Duplicate asset uid {0} on {1}", uid, file.string());
		uid = GenerateUid(used);
		rewrite = true;
	}
	else if (!meta.kind || *meta.kind != *kind)
	{
		rewrite = true;
	}

	used.insert(uid);
	if (rewrite && !WriteMeta(metaPath, uid, *kind))
		Warn_Core("Failed to write asset meta for {0}", file.string());

	AssetRecord record;
	record.uid = uid;
	record.kind = *kind;
	record.path = AssetDatabase::NormalizePath(file);
	records[uid] = std::move(record);
}

void Walk(
	const std::filesystem::path& directory,
	std::unordered_map<uint64_t, AssetRecord>& records,
	std::unordered_set<uint64_t>& used)
{
	std::error_code ec;
	std::filesystem::directory_iterator it(
		directory,
		std::filesystem::directory_options::skip_permission_denied,
		ec);
	if (ec)
	{
		Warn_Core("Failed to scan assets in {0}: {1}", directory.string(), ec.message());
		return;
	}

	for (const std::filesystem::directory_entry& entry : it)
	{
		std::error_code entryEc;
		if (entry.is_directory(entryEc))
		{
			if (!entryEc)
				Walk(entry.path(), records, used);
			continue;
		}
		if (entryEc || !entry.is_regular_file(entryEc))
			continue;
		ConsiderFile(entry.path(), records, used);
	}
}

} // namespace

std::string AssetDatabase::NormalizePath(const std::filesystem::path& path)
{
	std::error_code ec;
	std::filesystem::path absolute = std::filesystem::absolute(path, ec);
	if (ec)
		absolute = path;
	return absolute.lexically_normal().string();
}

void AssetDatabase::Sync(const std::filesystem::path& root)
{
	std::unordered_map<uint64_t, AssetRecord> next;
	std::error_code ec;
	if (std::filesystem::exists(root, ec) && std::filesystem::is_directory(root, ec))
	{
		try
		{
			std::unordered_set<uint64_t> used;
			Walk(root, next, used);
		}
		catch (const std::exception& e)
		{
			Error_Core("Asset scan failed: {0}", e.what());
		}
	}

	std::unique_lock lock(s_Mutex);
	s_Records.swap(next);
}

std::vector<AssetRecord> AssetDatabase::GetByKind(AssetKind kind)
{
	std::vector<AssetRecord> result;
	{
		std::shared_lock lock(s_Mutex);
		for (const auto& [uid, record] : s_Records)
		{
			if (record.kind == kind)
				result.push_back(record);
		}
	}
	std::sort(result.begin(), result.end(),
		[](const AssetRecord& a, const AssetRecord& b)
		{
			return a.path < b.path;
		});
	return result;
}

} // namespace Engine
