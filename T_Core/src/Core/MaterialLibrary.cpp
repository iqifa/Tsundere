#include "MaterialLibrary.h"
#include "Debug/Debug.h"
#include "Platform/RHI/RHIShader.h"
#include <cmath>

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

glm::vec3 MaterialLibrary::DefaultMeshColor(int index)
{
	const float h = std::fmod(static_cast<float>(index) * 0.61803398875f, 1.0f);
	const float s = 0.55f;
	const float v = 0.86f;
	const float c = v * s;
	const float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
	const float m = v - c;
	glm::vec3 rgb(0.0f);
	switch (static_cast<int>(h * 6.0f) % 6)
	{
	case 0: rgb = glm::vec3(c, x, 0.0f); break;
	case 1: rgb = glm::vec3(x, c, 0.0f); break;
	case 2: rgb = glm::vec3(0.0f, c, x); break;
	case 3: rgb = glm::vec3(0.0f, x, c); break;
	case 4: rgb = glm::vec3(x, 0.0f, c); break;
	default: rgb = glm::vec3(c, 0.0f, x); break;
	}
	return rgb + glm::vec3(m);
}

Ref<Material> MaterialLibrary::CreateModelDefault(int meshIndex)
{
	auto material = CreateDefault("ModelPreview", ShaderLibiray::Get("res/shaders/SceneModelPreview.shader"));
	material->SetVec4("u_BaseColor", glm::vec4(DefaultMeshColor(meshIndex), 1.0f));
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
