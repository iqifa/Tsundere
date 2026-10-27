#pragma once

#include "Core/Core.h"
#include "Platform/RHI/RHIShader.h"
#include "Platform/RHI/RHITexture.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Engine {

// Material parameter types supported by the engine
struct MaterialParameter
{
	enum class Type
	{
		Float,
		Vec2,
		Vec3,
		Vec4,
		Int,
		Texture2D,
		TextureCube
	};

	Type type;
	std::variant<float, glm::vec2, glm::vec3, glm::vec4, int, Ref<RHITexture2D>> value;

	MaterialParameter() : type(Type::Float), value(0.0f) {}
	MaterialParameter(float v) : type(Type::Float), value(v) {}
	MaterialParameter(const glm::vec2& v) : type(Type::Vec2), value(v) {}
	MaterialParameter(const glm::vec3& v) : type(Type::Vec3), value(v) {}
	MaterialParameter(const glm::vec4& v) : type(Type::Vec4), value(v) {}
	MaterialParameter(int v) : type(Type::Int), value(v) {}
	MaterialParameter(Ref<RHITexture2D> v) : type(Type::Texture2D), value(v) {}

	template<typename T>
	T* Get()
	{
		return std::get_if<T>(&value);
	}

	template<typename T>
	const T* Get() const
	{
		return std::get_if<T>(&value);
	}
};

class T_API Material
{
public:
	Material() = default;
	Material(const std::string& name, Ref<RHIShader> shader);
	Material(const Material& other);
	Material& operator=(const Material& other);
	~Material() = default;

	// Shader management
	void SetShader(Ref<RHIShader> shader);
	Ref<RHIShader> GetShader() const { return m_Shader; }

	// Parameter management
	void SetParameter(const std::string& name, const MaterialParameter& param);
	MaterialParameter* GetParameter(const std::string& name);
	const MaterialParameter* GetParameter(const std::string& name) const;
	const std::unordered_map<std::string, MaterialParameter>& GetParameters() const { return m_Parameters; }

	// Convenience setters
	void SetFloat(const std::string& name, float value);
	void SetVec2(const std::string& name, const glm::vec2& value);
	void SetVec3(const std::string& name, const glm::vec3& value);
	void SetVec4(const std::string& name, const glm::vec4& value);
	void SetInt(const std::string& name, int value);
	void SetTexture(const std::string& name, Ref<RHITexture2D> texture);

	// Convenience getters
	float GetFloat(const std::string& name, float defaultValue = 0.0f) const;
	glm::vec2 GetVec2(const std::string& name, const glm::vec2& defaultValue = glm::vec2(0.0f)) const;
	glm::vec3 GetVec3(const std::string& name, const glm::vec3& defaultValue = glm::vec3(0.0f)) const;
	glm::vec4 GetVec4(const std::string& name, const glm::vec4& defaultValue = glm::vec4(0.0f)) const;
	int GetInt(const std::string& name, int defaultValue = 0) const;
	Ref<RHITexture2D> GetTexture(const std::string& name) const;

	// Metadata
	const std::string& GetName() const { return m_Name; }
	void SetName(const std::string& name) { m_Name = name; }
	const std::string& GetAssetPath() const { return m_AssetPath; }
	void SetAssetPath(const std::string& path) { m_AssetPath = path; }

	// Serialization
	bool Serialize(const std::string& filepath);
	bool Deserialize(const std::string& filepath);

	// Initialize parameters from shader reflection
	void InitializeParametersFromShader();

private:
	std::string m_Name;
	std::string m_AssetPath; // .tmat file path
	Ref<RHIShader> m_Shader;
	std::unordered_map<std::string, MaterialParameter> m_Parameters;

	// Helper to convert type string from shader reflection to MaterialParameter::Type
	static MaterialParameter::Type TypeFromString(const std::string& typeStr);
	static std::string TypeToString(MaterialParameter::Type type);
};

} // namespace Engine
