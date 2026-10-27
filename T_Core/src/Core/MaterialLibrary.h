#pragma once

#include "Core/Core.h"
#include "Material.h"
#include <unordered_map>
#include <shared_mutex>
#include <string>

namespace Engine {

class T_API MaterialLibrary
{
public:
	// Load material from .tmat file
	static Ref<Material> Load(const std::string& filepath);

	// Save material to .tmat file
	static bool Save(Ref<Material> material, const std::string& filepath);

	// Get material from cache (load if not cached)
	static Ref<Material> Get(const std::string& filepath);

	// Create a new material with the given shader
	static Ref<Material> CreateDefault(const std::string& name, Ref<RHIShader> shader);

	// Add material to cache
	static void Add(const std::string& filepath, Ref<Material> material);

	// Remove material from cache
	static void Remove(const std::string& filepath);

	// Clear all cached materials
	static void Clear();

	// Get all cached materials
	static const std::unordered_map<std::string, Ref<Material>>& GetMaterials();

	static std::shared_mutex s_Mutex;

private:
	static std::unordered_map<std::string, Ref<Material>> s_Materials;
};

} // namespace Engine
