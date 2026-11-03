#pragma once

// Path-based texture cache. The map lives in T_Core so the editor executable
// and the DLL share one copy, and Application can release the textures while
// the device is still alive.

#include "Platform/RHI/RHITexture.h"
#include <string>
#include <utility>
#include <vector>

class T_API TextureLibiary
{
public:
	static void Add(Ref<RHITexture2D> tex);
	static void Add(const std::string& path, Ref<RHITexture2D> tex);

	static Ref<RHITexture2D> Load(const std::string& filepath);
	static Ref<RHITexture2D> Get(const std::string& filepath);

	static void Remove(const std::string& path);
	static void Clear();

	// Snapshot for UI. The live map stays inside the DLL.
	static std::vector<std::pair<std::string, Ref<RHITexture2D>>> Entries();
};
