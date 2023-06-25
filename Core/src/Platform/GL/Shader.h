#pragma once

#include"HeadLine.h"
#include"ExternalFiles.h"

struct Uniform
{
	string Name;
	string Type;
};
struct ShaderProgramSource
{
	string VertexSource;
	string FragmentSource;
};
class Shader
{
private:
	string m_Name;
	unsigned int m_RendererID; 
	string m_FilePath;
	mutable unordered_map<string, int> m_UniformLocationCache;
public:
	vector<Uniform> uniform;
public:
	Shader(const string& filepath, const string& name );
	Shader(const string& filepath);
	~Shader();
	Shader(){}

	void Bind() const;
	void UnBind() const;

	const string& GetName() { return m_Name; }
	const string& GetPath() { return m_FilePath; }

	//Set Uniforms
	void SetUniform4f(const string& name, float v0, float v1, float v2, float v3) const;
	void SetUniform1i(const string& name, int value) const;
	void SetUniformMat4f(const string& name, const  mat4& mat4)const;
	void SetUniformVec3(const string& name, const vec3& value)const;

	static Ref<Shader>Create(const string& filepath, const string& name);
	static Ref<Shader>Create(const string& filepath);

	inline unsigned int GetID()const { return m_RendererID; }
private:
	unsigned int CompileShader(unsigned int type, const string& source);
	ShaderProgramSource ParseShader(const string& filepath);
	unsigned int CreateShader(const string& vertexShader, const string& fragmentShader);
	//unsigned int CreateShader(const string& name, const string& vertexShader, const string& fragmentShader);
	int GetUniformLocation(const string& name)const;

};
class ShaderLibiray {
public:

	static void Add(const Ref<Shader>& shader);
	static Ref<Shader> Load(const string& FilePath);
	static Ref<Shader> Load(const string& name,const string& FilePath);

	static Ref<Shader> Get(const string& path);
private:
	static unordered_map<string, Ref<Shader>>m_Shaders;
	
};

