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
#include <sstream>
#include <set>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

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
	if (ext == ".tcubemap")
		return AssetKind::Cubemap;
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
	case AssetKind::Cubemap: return "Cubemap";
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
	if (text == "Cubemap") { kind = AssetKind::Cubemap; return true; }
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
	std::vector<std::string> extra;
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
		else if (!line.empty()) meta.extra.push_back(line);
	}
	return meta;
}

bool WriteMeta(const std::filesystem::path& metaPath, uint64_t uid, AssetKind kind,
	const std::vector<std::string>* extra = nullptr)
{
	const auto preserved = extra ? *extra : ReadMeta(metaPath).extra;
	auto temporary = metaPath;
	temporary += ".tmp";
	std::ofstream file(temporary, std::ios::trunc);
	if (!file.is_open())
		return false;
	file << "uid: " << uid << "\n";
	file << "type: " << KindToString(kind) << "\n";
	for (const auto& line : preserved) file << line << '\n';
	file.close();
	if (file && MoveFileExW(temporary.c_str(), metaPath.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
	std::error_code ignored;
	std::filesystem::remove(temporary, ignored);
	return false;
}

std::set<uint32_t> DisabledIndices(const ParsedMeta& meta)
{
	std::set<uint32_t> indices;
	for (const auto& line : meta.extra)
		if (line.rfind("disabled_submeshes:", 0) == 0)
		{
			std::istringstream in(line.substr(19));
			uint32_t index;
			while (in >> index) indices.insert(index);
		}
	return indices;
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
	std::unique_lock lock(s_Mutex);
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

	s_Records.swap(next);
}

std::vector<uint32_t> AssetDatabase::DisabledSubmeshes(const std::filesystem::path& model)
{
	std::shared_lock lock(s_Mutex);
	const auto indices = DisabledIndices(ReadMeta(MetaPathFor(model)));
	return { indices.begin(), indices.end() };
}

bool AssetDatabase::SetSubmeshActive(const std::filesystem::path& model, uint32_t index,
	bool active, std::string& error)
{
	try
	{
		std::unique_lock lock(s_Mutex);
		if (!std::filesystem::is_regular_file(model))
			throw std::runtime_error("Model file no longer exists");
		const auto path = MetaPathFor(model);
		auto meta = ReadMeta(path);
		auto indices = DisabledIndices(meta);
		if (active) indices.erase(index); else indices.insert(index);
		meta.extra.erase(std::remove_if(meta.extra.begin(), meta.extra.end(), [](const auto& line) {
			return line.rfind("disabled_submeshes:", 0) == 0;
		}), meta.extra.end());
		std::ostringstream field;
		field << "disabled_submeshes:";
		for (auto disabled : indices) field << ' ' << disabled;
		meta.extra.push_back(field.str());
		if (!meta.uid)
		{
			std::unordered_set<uint64_t> used;
			for (const auto& entry : s_Records) used.insert(entry.first);
			meta.uid = GenerateUid(used);
		}
		if (!WriteMeta(path, meta.uid, AssetKind::Model, &meta.extra))
			throw std::runtime_error("Cannot save model metadata: " + path.string());
		error.clear();
		return true;
	}
	catch (const std::exception& e) { error = e.what(); return false; }
}

uint64_t AssetDatabase::UidForPath(const std::filesystem::path& path, AssetKind kind)
{
	const auto normalized = NormalizePath(path);
	std::shared_lock lock(s_Mutex);
	for (const auto& [uid, record] : s_Records)
		if (record.kind == kind && record.path == normalized) return uid;
	return 0;
}

std::string AssetDatabase::Resolve(uint64_t uid, AssetKind kind)
{
	std::shared_lock lock(s_Mutex);
	const auto found = s_Records.find(uid);
	return found != s_Records.end() && found->second.kind == kind ? found->second.path : std::string{};
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
