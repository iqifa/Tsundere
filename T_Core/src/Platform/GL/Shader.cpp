#include "Shader.h"
#include"Debug/Debug.h"
#include"Platform/ShaderParser.h"
#include<shared_mutex>
using namespace std;

Shader::Shader(const string& filepath, const string& name) :m_FilePath(filepath), m_RendererID(0), m_Name(name)
{
	ParsedShader parsed = ParseShaderFile(filepath);
	uniform = std::move(parsed.uniforms);

	auto& sources = parsed.sources;
	auto itComp = sources.find(ShaderStage::Compute);
	if (itComp != sources.end())
	{
		m_RendererID = CreateComputeShader(itComp->second);
	}
	else
	{
		auto itVert = sources.find(ShaderStage::Vertex);
		auto itFrag = sources.find(ShaderStage::Fragment);
		string vertSrc = (itVert != sources.end()) ? itVert->second : "";
		string fragSrc = (itFrag != sources.end()) ? itFrag->second : "";
		if (!vertSrc.empty() || !fragSrc.empty())
			m_RendererID = CreateShader(vertSrc, fragSrc);
	}

	cout << "\033[1;32mSuccessful Parse Shader:" + m_Name + "!\033[0m" << endl;
}

Shader::Shader(const string& filepath) :m_FilePath(filepath), m_RendererID(0)
{
	auto LastSlash = filepath.find_last_of("/");
	LastSlash = LastSlash == string::npos ? 0 : LastSlash + 1;
	auto LastDot = filepath.find_last_of(".");
	LastDot = LastSlash == string::npos ? filepath.length() : LastDot;
	auto count = LastDot - LastSlash;
	m_Name = filepath.substr(LastSlash, count);

	ParsedShader parsed = ParseShaderFile(filepath);
	uniform = std::move(parsed.uniforms);

	auto& sources = parsed.sources;
	auto itComp = sources.find(ShaderStage::Compute);
	if (itComp != sources.end())
	{
		m_RendererID = CreateComputeShader(itComp->second);
	}
	else
	{
		auto itVert = sources.find(ShaderStage::Vertex);
		auto itFrag = sources.find(ShaderStage::Fragment);
		string vertSrc = (itVert != sources.end()) ? itVert->second : "";
		string fragSrc = (itFrag != sources.end()) ? itFrag->second : "";
		if (!vertSrc.empty() || !fragSrc.empty())
			m_RendererID = CreateShader(vertSrc, fragSrc);
	}

	cout << "\033[1;32mSuccessful Parse Shader:" + m_Name + "!\033[0m" << endl;
}


Shader::~Shader()
{
	glDeleteProgram(m_RendererID);
}

void Shader::Bind()const
{
	glUseProgram(m_RendererID);
}

void Shader::UnBind()const
{
	glUseProgram(0);
}



unsigned int  Shader::CompileShader(unsigned int type, const string& source)
{
	unsigned int id = glCreateShader(type);
	const char* src = source.c_str();
	glShaderSource(id, 1, &src, nullptr);
	glCompileShader(id);

	int result;
	glGetShaderiv(id, GL_COMPILE_STATUS, &result);
	if (result == GL_FALSE)
	{
		int lenth;
		glGetShaderiv(id, GL_INFO_LOG_LENGTH, &lenth);
		char* message = new char[lenth];
		glGetShaderInfoLog(id, lenth, &lenth, message);
		string info = "Failed to Compile ["+m_Name +"] " + (string)(type == GL_VERTEX_SHADER ? "vertex" : (type == GL_FRAGMENT_SHADER ? "fragment" : "compute")) + " Shader!";
		Warn_Core(info)
		cout << message << endl;
		glDeleteShader(id);
		return 0;
	}
	return id;
}

Ref<Shader> Shader::Create(const string& filepath, const string& name)
{
	return CreateRef<Shader>(filepath, name);
}

Ref<Shader> Shader::Create(const string& filepath)
{
	return CreateRef<Shader>(filepath);
}

unsigned int Shader::CreateShader(const string& vertexShader, const string& fragmentShader)
{
	unsigned int program = glCreateProgram();
	unsigned int vs = CompileShader(GL_VERTEX_SHADER, vertexShader);
	unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShader);

	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glValidateProgram(program);

	glDeleteShader(vs);
	glDeleteShader(fs);

	return program;
}

unsigned int Shader::CreateComputeShader(const string& computeSource)
{
	unsigned int cs = CompileShader(GL_COMPUTE_SHADER, computeSource);
	if (cs == 0)
		return 0;

	unsigned int program = glCreateProgram();
	glAttachShader(program, cs);
	glLinkProgram(program);
	glValidateProgram(program);

	glDeleteShader(cs);

	return program;
}

void Shader::DispatchCompute(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ) const
{
	glDispatchCompute(groupsX, groupsY, groupsZ);
}

Ref<Shader> Shader::CreateCompute(const string& filepath)
{
	return CreateRef<Shader>(filepath);
}

void Shader::SetUniform4f(const string& name, float v0, float v1, float v2, float v3)const
{
	glUniform4f(GetUniformLocation(name), v0, v1, v2, v3);
}

void Shader::SetUniform1f(const string& name, float value)const
{
	glUniform1f(GetUniformLocation(name), value);
}

void Shader::SetUniform1i(const string& name, int value)const
{
	glUniform1i(GetUniformLocation(name), value);
}

void Shader::SetUniformMat4f(const string& name, const mat4& mat4)const
{
	glUniformMatrix4fv(GetUniformLocation(name), 1, false, &mat4[0][0]);
}

void Shader::SetUniformVec3(const string& name, const vec3& value) const
{
	glUniform3fv(GetUniformLocation(name), 1, &value[0]);
}
void Shader::SetUniformVec2(const string& name, const vec2& value) const
{
	glUniform2fv(GetUniformLocation(name), 1, &value[0]);
}


int Shader::GetUniformLocation(const string& name)  const
{
	if (m_UniformLocationCache.find(name) != m_UniformLocationCache.end())
		return m_UniformLocationCache[name];

	int locatation = glGetUniformLocation(m_RendererID, name.c_str());
	if (locatation == -1)
		Warn_Core("["+m_Name + "]:Warning: uniform '" + name + "' doesnt exist!");
	m_UniformLocationCache[name] = locatation;
	return locatation;
}

// --- ShaderLibiray ---

shared_mutex ShaderLibiray::s_Mutex;
unordered_map<string, Ref<RHIShader>> ShaderLibiray::m_Shaders;

void ShaderLibiray::Add(const Ref<RHIShader>& shader)
{
	std::unique_lock lock(s_Mutex);
	auto& path = shader->GetPath();
	m_Shaders[path] = shader;
}

Ref<RHIShader> ShaderLibiray::Load(const string& FilePath)
{
	{
		std::shared_lock lock(s_Mutex);
		auto it = m_Shaders.find(FilePath);
		if (it != m_Shaders.end())
			return it->second;
	}
	auto shader = Shader::Create(FilePath);  // Ref<Shader> → Ref<RHIShader> implicit upcast
	{
		std::unique_lock lock(s_Mutex);
		m_Shaders[FilePath] = shader;
	}
	return shader;
}

Ref<RHIShader> ShaderLibiray::Load(const string& name, const string& FilePath)
{
	{
		std::shared_lock lock(s_Mutex);
		auto it = m_Shaders.find(FilePath);
		if (it != m_Shaders.end())
			return it->second;
	}
	auto shader = Shader::Create(FilePath, name);  // Ref<Shader> → Ref<RHIShader>
	{
		std::unique_lock lock(s_Mutex);
		m_Shaders[FilePath] = shader;
	}
	return shader;
}

Ref<RHIShader> ShaderLibiray::Get(const string& path)
{
	{
		std::shared_lock lock(s_Mutex);
		auto it = m_Shaders.find(path);
		if (it != m_Shaders.end())
			return it->second;
	}
	return Load(path);
}
