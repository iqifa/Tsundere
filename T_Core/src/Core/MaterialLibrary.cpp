#include "MaterialLibrary.h"
#include "Core/Log.h"

namespace Engine {

std::unordered_map<std::string, Ref<Material>> MaterialLibrary::s_Materials;
std::shared_mutex MaterialLibrary::s_Mutex;

Ref<Material> MaterialLibrary::Load(const std::string& filepath)
{
	Ref<Material> material = CreateRef<Material>();
	if (material->Deserialize(filepath))
	{
		std::unique_lock lock(s_Mutex);
		s_Materials[filepath] = material;
		return material;
	}
	return nullptr;
}

bool MaterialLibrary::Save(Ref<Material> material, const std::string& filepath)
{
	if (!material)
		return false;

	if (material->Serialize(filepath))
	{
		std::unique_lock lock(s_Mutex);
		s_Materials[filepath] = material;
		return true;
	}
	return false;
}

Ref<Material> MaterialLibrary::Get(const std::string& filepath)
{
	{
		std::shared_lock lock(s_Mutex);
		auto it = s_Materials.find(filepath);
		if (it != s_Materials.end())
			return it->second;
	}

	// Not in cache, try to load
	return Load(filepath);
}

Ref<Material> MaterialLibrary::CreateDefault(const std::string& name, Ref<RHIShader> shader)
{
	Ref<Material> material = CreateRef<Material>(name, shader);
	return material;
}

void MaterialLibrary::Add(const std::string& filepath, Ref<Material> material)
{
	std::unique_lock lock(s_Mutex);
	s_Materials[filepath] = material;
}

void MaterialLibrary::Remove(const std::string& filepath)
{
	std::unique_lock lock(s_Mutex);
	s_Materials.erase(filepath);
}

void MaterialLibrary::Clear()
{
	std::unique_lock lock(s_Mutex);
	s_Materials.clear();
}

const std::unordered_map<std::string, Ref<Material>>& MaterialLibrary::GetMaterials()
{
	return s_Materials;
}

} // namespace Engine
