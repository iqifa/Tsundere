#include "Material.h"
#include "Platform/RHI/RHITextureLibrary.h"
#include "Debug/Debug.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace Engine {

Material::Material(const std::string& name, Ref<RHIShader> shader)
	: m_Name(name), m_Shader(shader)
{
	if (m_Shader)
		InitializeParametersFromShader();
}

Material::Material(const Material& other)
	: m_Name(other.m_Name)
	, m_AssetPath(other.m_AssetPath)
	, m_Shader(other.m_Shader)
	, m_Parameters(other.m_Parameters)
{
}

Material& Material::operator=(const Material& other)
{
	if (this != &other)
	{
		m_Name = other.m_Name;
		m_AssetPath = other.m_AssetPath;
		m_Shader = other.m_Shader;
		m_Parameters = other.m_Parameters;
	}
	return *this;
}

void Material::SetShader(Ref<RHIShader> shader)
{
	m_Shader = shader;

	// The new shader defines a different parameter set: drop everything rather
	// than letting the previous shader's entries linger as dead UI/uniforms.
	// (InitializeParametersFromShader only fills gaps; it never removes.)
	m_Parameters.clear();

	if (m_Shader)
		InitializeParametersFromShader();
}

void Material::SetParameter(const std::string& name, const MaterialParameter& param)
{
	m_Parameters[name] = param;
}

MaterialParameter* Material::GetParameter(const std::string& name)
{
	auto it = m_Parameters.find(name);
	return it != m_Parameters.end() ? &it->second : nullptr;
}

const MaterialParameter* Material::GetParameter(const std::string& name) const
{
	auto it = m_Parameters.find(name);
	return it != m_Parameters.end() ? &it->second : nullptr;
}

void Material::SetFloat(const std::string& name, float value)
{
	m_Parameters[name] = MaterialParameter(value);
}

void Material::SetVec2(const std::string& name, const glm::vec2& value)
{
	m_Parameters[name] = MaterialParameter(value);
}

void Material::SetVec3(const std::string& name, const glm::vec3& value)
{
	m_Parameters[name] = MaterialParameter(value);
}

void Material::SetVec4(const std::string& name, const glm::vec4& value)
{
	m_Parameters[name] = MaterialParameter(value);
}

void Material::SetInt(const std::string& name, int value)
{
	m_Parameters[name] = MaterialParameter(value);
}

void Material::SetTexture(const std::string& name, Ref<RHITexture2D> texture)
{
	m_Parameters[name] = MaterialParameter(texture);
}

float Material::GetFloat(const std::string& name, float defaultValue) const
{
	const auto* param = GetParameter(name);
	if (param && param->type == MaterialParameter::Type::Float)
	{
		const float* val = param->Get<float>();
		return val ? *val : defaultValue;
	}
	return defaultValue;
}

glm::vec2 Material::GetVec2(const std::string& name, const glm::vec2& defaultValue) const
{
	const auto* param = GetParameter(name);
	if (param && param->type == MaterialParameter::Type::Vec2)
	{
		const glm::vec2* val = param->Get<glm::vec2>();
		return val ? *val : defaultValue;
	}
	return defaultValue;
}

glm::vec3 Material::GetVec3(const std::string& name, const glm::vec3& defaultValue) const
{
	const auto* param = GetParameter(name);
	if (param && param->type == MaterialParameter::Type::Vec3)
	{
		const glm::vec3* val = param->Get<glm::vec3>();
		return val ? *val : defaultValue;
	}
	return defaultValue;
}

glm::vec4 Material::GetVec4(const std::string& name, const glm::vec4& defaultValue) const
{
	const auto* param = GetParameter(name);
	if (param && param->type == MaterialParameter::Type::Vec4)
	{
		const glm::vec4* val = param->Get<glm::vec4>();
		return val ? *val : defaultValue;
	}
	return defaultValue;
}

int Material::GetInt(const std::string& name, int defaultValue) const
{
	const auto* param = GetParameter(name);
	if (param && param->type == MaterialParameter::Type::Int)
	{
		const int* val = param->Get<int>();
		return val ? *val : defaultValue;
	}
	return defaultValue;
}

Ref<RHITexture2D> Material::GetTexture(const std::string& name) const
{
	const auto* param = GetParameter(name);
	if (param && param->type == MaterialParameter::Type::Texture2D)
	{
		const Ref<RHITexture2D>* tex = param->Get<Ref<RHITexture2D>>();
		return tex ? *tex : nullptr;
	}
	return nullptr;
}

size_t Material::PackMaterialBlock(
	const ShaderReflection& refl,
	std::vector<char>& out, uint32_t binding) const
{
	out.clear();

	// Material-parameter block by convention: binding >= 3, the first such
	// UBO in the shader. Engine blocks (0/1/2) belong to the render passes.
	const ShaderBlock* materialBlock = nullptr;
	for (const auto& block : refl.UniformBlocks)
	{
		if (block.Set == 0 && block.Binding >= 3 && (binding == UINT32_MAX || binding == block.Binding))
		{
			materialBlock = &block;
			break;
		}
	}
	if (!materialBlock || materialBlock->Size == 0)
		return 0;

	out.assign(materialBlock->Size, 0);
	for (const auto& member : materialBlock->Members)
	{
		if (member.Size == 0 || member.Offset + member.Size > out.size())
			continue;

		const auto* param = GetParameter(member.Name);
		if (!param)
			continue;

		auto writeRaw = [&](const void* src, size_t bytes)
		{
			const size_t n = std::min(bytes, static_cast<size_t>(member.Size));
			std::memcpy(out.data() + member.Offset, src, n);
		};

		// std140 scalar/vector sizes match the GLM types for the shapes the
		// material editor exposes (float, vec2..4, int); arrays are not.
		switch (param->type)
		{
		case MaterialParameter::Type::Float:
			if (const float* v = param->Get<float>()) writeRaw(v, sizeof(float));
			break;
		case MaterialParameter::Type::Int:
			if (const int* v = param->Get<int>()) writeRaw(v, sizeof(int));
			break;
		case MaterialParameter::Type::Vec2:
			if (const glm::vec2* v = param->Get<glm::vec2>()) writeRaw(v, sizeof(glm::vec2));
			break;
		case MaterialParameter::Type::Vec3:
			if (const glm::vec3* v = param->Get<glm::vec3>()) writeRaw(v, sizeof(glm::vec3));
			break;
		case MaterialParameter::Type::Vec4:
			if (const glm::vec4* v = param->Get<glm::vec4>()) writeRaw(v, sizeof(glm::vec4));
			break;
		default:
			break;
		}
	}
	return out.size();
}

void Material::ApplyToShader(Ref<RHIShader> shader) const
{
	if (!shader)
		return;

	for (const auto& [name, param] : m_Parameters)
	{
		// Skip uniforms the target program does not have (loose uniforms only;
		// block members are uploaded via descriptor sets, not by name).
		if (!shader->HasUniform(name))
			continue;

		switch (param.type)
		{
		case MaterialParameter::Type::Float:
			if (const float* val = param.Get<float>())
				shader->SetUniform1f(name, *val);
			break;
		case MaterialParameter::Type::Int:
			if (const int* val = param.Get<int>())
				shader->SetUniform1i(name, *val);
			break;
		case MaterialParameter::Type::Vec2:
			if (const glm::vec2* val = param.Get<glm::vec2>())
				shader->SetUniformVec2(name, *val);
			break;
		case MaterialParameter::Type::Vec3:
			if (const glm::vec3* val = param.Get<glm::vec3>())
				shader->SetUniformVec3(name, *val);
			break;
		case MaterialParameter::Type::Vec4:
			if (const glm::vec4* val = param.Get<glm::vec4>())
				shader->SetUniform4f(name, val->x, val->y, val->z, val->w);
			break;
		default:
			break;
		}
	}
}

void Material::InitializeParametersFromShader()
{
	if (!m_Shader)
		return;

	const ShaderReflection& refl = m_Shader->GetReflection();

	// Extract uniform blocks (UBOs). Engine-owned blocks (binding 0/1/2 =
	// PerPass / PerFrame / Camera) are uploaded by the render passes; only
	// material-parameter blocks (binding >= 3, e.g. MaterialParams) and
	// non-pad members become editable parameters.
	for (const auto& block : refl.UniformBlocks)
	{
		if (block.Binding < 3)
			continue;

		for (const auto& member : block.Members)
		{
			// Skip std140 padding helpers — not user-visible parameters.
			if (member.Name.rfind("_pad", 0) == 0 || member.Name.rfind("_matpad", 0) == 0)
				continue;

			// Skip if parameter already exists (preserve user-set values)
			if (m_Parameters.find(member.Name) != m_Parameters.end())
				continue;

			MaterialParameter::Type type = TypeFromString(member.Type);
			switch (type)
			{
			// Keep tint and tiling neutral. UV offsets and emissive strength
			// start at zero so a new material shows its texture under scene lighting.
			case MaterialParameter::Type::Float:
				m_Parameters[member.Name] = MaterialParameter((member.Name == "u_EmissiveStrength" || member.Name == "u_Metallic") ? 0.0f : 0.5f);
				break;
			case MaterialParameter::Type::Vec2:
				m_Parameters[member.Name] = MaterialParameter(glm::vec2(member.Name == "u_Offset" ? 0.0f : 1.0f));
				break;
			case MaterialParameter::Type::Vec3:
				m_Parameters[member.Name] = MaterialParameter(glm::vec3(1.0f));
				break;
			case MaterialParameter::Type::Vec4:
				m_Parameters[member.Name] = MaterialParameter(glm::vec4(1.0f));
				break;
			case MaterialParameter::Type::Int:
				m_Parameters[member.Name] = MaterialParameter(0);
				break;
			default:
				break;
			}
		}
	}

	// Extract sampled images (textures)
	for (const auto& sampler : refl.SampledImages)
	{
		if (IsSceneTexture(sampler.Name) || m_Parameters.find(sampler.Name) != m_Parameters.end())
			continue;

		m_Parameters[sampler.Name] = MaterialParameter(Ref<RHITexture2D>(nullptr));
	}
}

bool Material::Serialize(const std::string& filepath)
{
	std::ofstream file(filepath);
	if (!file.is_open())
	{
		Error_Core("Failed to open file for writing: {0}", filepath);
		return false;
	}

	// Write header
	file << "# Tsundere Material File\n";
	file << "Name: " << m_Name << "\n";
	file << "Shader: " << (m_Shader ? m_Shader->GetPath() : "") << "\n";
	file << "\n[Parameters]\n";

	// Write parameters
	for (const auto& [name, param] : m_Parameters)
	{
		file << name << " = ";

		switch (param.type)
		{
		case MaterialParameter::Type::Float:
			if (const float* val = param.Get<float>())
				file << "float " << *val;
			break;
		case MaterialParameter::Type::Vec2:
			if (const glm::vec2* val = param.Get<glm::vec2>())
				file << "vec2 " << val->x << " " << val->y;
			break;
		case MaterialParameter::Type::Vec3:
			if (const glm::vec3* val = param.Get<glm::vec3>())
				file << "vec3 " << val->x << " " << val->y << " " << val->z;
			break;
		case MaterialParameter::Type::Vec4:
			if (const glm::vec4* val = param.Get<glm::vec4>())
				file << "vec4 " << val->x << " " << val->y << " " << val->z << " " << val->w;
			break;
		case MaterialParameter::Type::Int:
			if (const int* val = param.Get<int>())
				file << "int " << *val;
			break;
		case MaterialParameter::Type::Texture2D:
			if (const Ref<RHITexture2D>* tex = param.Get<Ref<RHITexture2D>>())
			{
				if (*tex)
					file << "texture2d " << (*tex)->GetPath();
				else
				{
					file << "texture2d";
				}
			}
			break;
		default:
			break;
		}

		file << "\n";
	}

	file.close();
	m_AssetPath = filepath;
	Info_Core("Saved material to: {0}", filepath);
	return true;
}

bool Material::Deserialize(const std::string& filepath)
{
	std::ifstream file(filepath);
	if (!file.is_open())
	{
		Error_Core("Failed to open file for reading: {0}", filepath);
		return false;
	}

	m_Parameters.clear();
	std::string line;
	bool inParametersSection = false;

	while (std::getline(file, line))
	{
		// Skip comments and empty lines
		if (line.empty() || line[0] == '#')
			continue;

		// Parse header fields
		if (line.find("Name:") == 0)
		{
			m_Name = line.substr(5);
			// Trim whitespace
			m_Name.erase(0, m_Name.find_first_not_of(" \t"));
			m_Name.erase(m_Name.find_last_not_of(" \t\r\n") + 1);
		}
		else if (line.find("Shader:") == 0)
		{
			std::string shaderPath = line.substr(7);
			shaderPath.erase(0, shaderPath.find_first_not_of(" \t"));
			shaderPath.erase(shaderPath.find_last_not_of(" \t\r\n") + 1);
			if (!shaderPath.empty())
				m_Shader = ShaderLibiray::Get(shaderPath);
		}
		else if (line.find("[Parameters]") == 0)
		{
			inParametersSection = true;
		}
		else if (inParametersSection && line.find('=') != std::string::npos)
		{
			// Parse parameter line: "name = type value..."
			size_t eqPos = line.find('=');
			std::string paramName = line.substr(0, eqPos);
			std::string valueStr = line.substr(eqPos + 1);

			// Trim whitespace
			paramName.erase(0, paramName.find_first_not_of(" \t"));
			paramName.erase(paramName.find_last_not_of(" \t") + 1);
			valueStr.erase(0, valueStr.find_first_not_of(" \t"));
			valueStr.erase(valueStr.find_last_not_of(" \t\r\n") + 1);

			std::istringstream iss(valueStr);
			std::string typeStr;
			iss >> typeStr;

			if (typeStr == "float")
			{
				float val;
				iss >> val;
				SetFloat(paramName, val);
			}
			else if (typeStr == "vec2")
			{
				glm::vec2 val;
				iss >> val.x >> val.y;
				SetVec2(paramName, val);
			}
			else if (typeStr == "vec3")
			{
				glm::vec3 val;
				iss >> val.x >> val.y >> val.z;
				SetVec3(paramName, val);
			}
			else if (typeStr == "vec4")
			{
				glm::vec4 val;
				iss >> val.x >> val.y >> val.z >> val.w;
				SetVec4(paramName, val);
			}
			else if (typeStr == "int")
			{
				int val;
				iss >> val;
				SetInt(paramName, val);
			}
			else if (typeStr == "texture2d")
			{
				std::string texPath;
				std::getline(iss, texPath);
				texPath.erase(0, texPath.find_first_not_of(" \t"));
				texPath.erase(texPath.find_last_not_of(" \t\r\n") + 1);
				if (!texPath.empty())
				{
					Ref<RHITexture2D> tex = TextureLibiary::Get(texPath);
					SetTexture(paramName, tex);
				}
			}
		}
	}

	file.close();
	m_AssetPath = filepath;
	if (m_Shader)
	{
		// .tmat files written before the reflection rework can hold garbage:
		// engine block members (PerFrame_Camera etc.) as scalar params,
		// _pad helpers, and mat4 members flattened to a single float. Drop
		// anything that does not match the shader's own material block, then
		// InitializeParametersFromShader fills the gaps with identity values.
		const ShaderReflection& refl = m_Shader->GetReflection();
		std::unordered_set<std::string> valid;
		for (const auto& block : refl.UniformBlocks)
		{
			if (block.Binding < 3)
				continue;
			for (const auto& member : block.Members)
			{
				if (member.Name.rfind("_pad", 0) == 0 || member.Name.rfind("_matpad", 0) == 0)
					continue;
				valid.insert(member.Name);
			}
		}
		for (const auto& sampler : refl.SampledImages)
			if (!IsSceneTexture(sampler.Name))
				valid.insert(sampler.Name);

		for (auto it = m_Parameters.begin(); it != m_Parameters.end();)
		{
			if (!valid.count(it->first))
				it = m_Parameters.erase(it);
			else
				++it;
		}

		InitializeParametersFromShader();
	}
	Info_Core("Loaded material from: {0}", filepath);
	return true;
}

MaterialParameter::Type Material::TypeFromString(const std::string& typeStr)
{
	std::string lower = typeStr;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

	if (lower == "float" || lower == "f32")
		return MaterialParameter::Type::Float;
	if (lower == "vec2" || lower == "float2")
		return MaterialParameter::Type::Vec2;
	if (lower == "vec3" || lower == "float3")
		return MaterialParameter::Type::Vec3;
	if (lower == "vec4" || lower == "float4")
		return MaterialParameter::Type::Vec4;
	if (lower == "int" || lower == "i32" || lower == "uint" || lower == "u32")
		return MaterialParameter::Type::Int;
	if (lower.find("sampler2d") != std::string::npos || lower.find("texture2d") != std::string::npos)
		return MaterialParameter::Type::Texture2D;
	if (lower.find("samplercube") != std::string::npos || lower.find("texturecube") != std::string::npos)
		return MaterialParameter::Type::TextureCube;

	return MaterialParameter::Type::Float; // Default fallback
}

std::string Material::TypeToString(MaterialParameter::Type type)
{
	switch (type)
	{
	case MaterialParameter::Type::Float: return "float";
	case MaterialParameter::Type::Vec2: return "vec2";
	case MaterialParameter::Type::Vec3: return "vec3";
	case MaterialParameter::Type::Vec4: return "vec4";
	case MaterialParameter::Type::Int: return "int";
	case MaterialParameter::Type::Texture2D: return "texture2d";
	case MaterialParameter::Type::TextureCube: return "texturecube";
	default: return "unknown";
	}
}

} // namespace Engine
