#pragma once

#include"HeadLine.h"
#include"ExternalFiles.h"

using namespace glm;

struct Uniform
{
	std::string Name;
	std::string Type;
};
struct ShaderProgramSource
{
	std::string VertexSource;
	std::string FragmentSource;
};
class Shader
{
private:
	std::string m_Name;
	unsigned int m_RendererID; 
	std::string m_FilePath;
	mutable std::unordered_map<std::string, int> m_UniformLocationCache;
public:
	std::vector<Uniform> uniform;
public:
	Shader(const std::string& filepath, const std::string& name );
	Shader(const std::string& filepath);
	~Shader();
	Shader(){}

	void Bind() const;
	void UnBind() const;

	const std::string& GetName() { return m_Name; }
	const std::string& GetPath() { return m_FilePath; }

	//Set Uniforms
	void SetUniform4f(const std::string& name, float v0, float v1, float v2, float v3) const;
	void SetUniform1i(const std::string& name, int value) const;
	void SetUniformMat4f(const std::string& name, const  mat4& mat4)const;
	void SetUniformVec3(const std::string& name, const vec3& value)const;

	static Ref<Shader>Create(const std::string& filepath, const std::string& name);
	static Ref<Shader>Create(const std::string& filepath);

	inline unsigned int GetID()const { return m_RendererID; }
private:
	unsigned int CompileShader(unsigned int type, const std::string& source);
	ShaderProgramSource ParseShader(const std::string& filepath);
	unsigned int CreateShader(const std::string& vertexShader, const std::string& fragmentShader);
	//unsigned int CreateShader(const string& name, const string& vertexShader, const string& fragmentShader);
	int GetUniformLocation(const std::string& name)const;

};
class ShaderLibiray {
public:

	static void Add(const Ref<Shader>& shader);
	static Ref<Shader> Load(const std::string& FilePath);
	static Ref<Shader> Load(const std::string& name,const std::string& FilePath);

	static Ref<Shader> Get(const std::string& path);
private:
	static std::unordered_map<std::string, Ref<Shader>>m_Shaders;
	
};

