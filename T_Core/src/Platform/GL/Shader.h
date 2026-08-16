#pragma once

#include"HeadLine.h"
#include"ExternalFiles.h"
#include"Core/Core.h"
#include"Platform/RHI/RHIShader.h"
#include<shared_mutex>
using namespace glm;

// Uniform struct is now defined in RHIShader.h (shared RHI type).

class T_API GLShader : public RHIShader
{
private:
	std::string m_Name;
	unsigned int m_RendererID;
	std::string m_FilePath;
	mutable std::unordered_map<std::string, int> m_UniformLocationCache;
public:
	std::vector<Uniform> uniform;   // kept public for backward compat; GetUniforms() returns this
public:
	GLShader(const std::string& filepath, const std::string& name );
	GLShader(const std::string& filepath);
	~GLShader();
	GLShader(){}

	// RHIShader interface
	void Bind() const override;
	void UnBind() const override;

	void DispatchCompute(unsigned int groupsX, unsigned int groupsY = 1, unsigned int groupsZ = 1) const override;

	const std::string& GetName() const override { return m_Name; }
	const std::string& GetPath() const override { return m_FilePath; }
	uint32_t GetID() const override { return m_RendererID; }
	const std::vector<Uniform>& GetUniforms() const override { return uniform; }

	// Uniform setters
	void SetUniform4f(const std::string& name, float v0, float v1, float v2, float v3) const override;
	void SetUniform1f(const std::string& name, float value) const override;
	void SetUniform1i(const std::string& name, int value) const override;
	void SetUniformMat4f(const std::string& name, const  glm::mat4& mat4)const override;
	void SetUniformVec3(const std::string& name, const glm::vec3& value)const override;
	void SetUniformVec2(const std::string& name, const glm::vec2& value)const override;

	// Legacy static factories — return Ref<Shader> for code that hasn't migrated yet
	static Ref<GLShader>Create(const std::string& filepath, const std::string& name);
	static Ref<GLShader>Create(const std::string& filepath);
	static Ref<GLShader>CreateCompute(const std::string& filepath);

private:
	unsigned int CompileShader(unsigned int type, const std::string& source);
	unsigned int CreateShader(const std::string& vertexShader, const std::string& fragmentShader);
	unsigned int CreateComputeShader(const std::string& computeSource);
	int GetUniformLocation(const std::string& name)const;
};

// --- RHI Factory methods (inline) ---
// Shader IS the GL implementation of RHIShader, so factories delegate to Shader::Create*.

inline Ref<RHIShader> RHIShader::Create(const std::string& filepath)
{
	return GLShader::Create(filepath);
}
inline Ref<RHIShader> RHIShader::Create(const std::string& filepath, const std::string& name)
{
	return GLShader::Create(filepath, name);
}
inline Ref<RHIShader> RHIShader::CreateCompute(const std::string& filepath)
{
	return GLShader::CreateCompute(filepath);
}

// ShaderLibiray — now stores RHIShader references
class T_API ShaderLibiray {
public:

	static void Add(const Ref<RHIShader>& shader);
	static Ref<RHIShader> Load(const std::string& FilePath);
	static Ref<RHIShader> Load(const std::string& name,const std::string& FilePath);

	static Ref<RHIShader> Get(const std::string& path);

	static std::shared_mutex s_Mutex;
private:
	static std::unordered_map<std::string, Ref<RHIShader>>m_Shaders;

};
