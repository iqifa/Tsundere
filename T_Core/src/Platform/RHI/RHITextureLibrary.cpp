#include "Platform/RHI/RHITextureLibrary.h"
#include "Debug/Debug.h"
#include "stb_image/stb_image.h"

#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>

namespace {

std::unordered_map<std::string, Ref<RHITexture2D>> g_Textures;
std::shared_mutex g_Mutex;

Ref<RHITexture2D> Find(const std::string& filepath)
{
	std::shared_lock lock(g_Mutex);
	auto it = g_Textures.find(filepath);
	if (it == g_Textures.end())
		return nullptr;
	return it->second;
}

} // namespace

void TextureLibiary::Add(const std::string& path, Ref<RHITexture2D> tex)
{
	if (!tex || path.empty())
		return;
	std::unique_lock lock(g_Mutex);
	g_Textures[path] = std::move(tex);
}

void TextureLibiary::Add(Ref<RHITexture2D> tex)
{
	if (!tex)
		return;
	const std::string path = tex->GetPath();
	Add(path, std::move(tex));
}

Ref<RHITexture2D> TextureLibiary::Load(const std::string& filepath)
{
	if (Ref<RHITexture2D> existing = Find(filepath))
		return existing;

	int width = 0;
	int height = 0;
	int channels = 0;
	unsigned char* pixels = Engine::LoadImageSTB(
		filepath.c_str(), &width, &height, &channels, 4, true);
	if (!pixels || width <= 0 || height <= 0)
	{
		if (pixels)
			stbi_image_free(pixels);
		Error_Core("Failed to load texture: {0}", filepath);
		return nullptr;
	}

	Texture2DDesc desc;
	desc.width = static_cast<uint32_t>(width);
	desc.height = static_cast<uint32_t>(height);
	desc.pixelData = pixels;
	desc.dataChannels = 4;
	desc.filePath = filepath;

	Ref<RHITexture2D> tex;
	try
	{
		tex = RHITexture2D::Create(desc);
	}
	catch (const std::exception& e)
	{
		stbi_image_free(pixels);
		Error_Core("Failed to create texture {0}: {1}", filepath, e.what());
		return nullptr;
	}
	stbi_image_free(pixels);

	if (tex)
		Add(filepath, tex);
	return tex;
}

Ref<RHITexture2D> TextureLibiary::Get(const std::string& filepath)
{
	if (Ref<RHITexture2D> existing = Find(filepath))
		return existing;
	return Load(filepath);
}

void TextureLibiary::Remove(const std::string& path)
{
	std::unique_lock lock(g_Mutex);
	g_Textures.erase(path);
}

void TextureLibiary::Clear()
{
	std::unique_lock lock(g_Mutex);
	g_Textures.clear();
}

std::vector<std::pair<std::string, Ref<RHITexture2D>>> TextureLibiary::Entries()
{
	std::vector<std::pair<std::string, Ref<RHITexture2D>>> entries;
	std::shared_lock lock(g_Mutex);
	entries.reserve(g_Textures.size());
	for (const auto& [path, texture] : g_Textures)
		entries.emplace_back(path, texture);
	return entries;
}
