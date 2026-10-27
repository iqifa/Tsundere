#include "GLShader.h"
#include"Debug/Debug.h"
#include"Platform/ShaderCompiler.h"
#include<cstring>
#include<shared_mutex>
#include<unordered_set>
using namespace std;





GLShader::GLShader(const string& filepath, const string& name) :m_FilePath(filepath), m_RendererID(0), m_Name(name)
{
	Build(filepath);
}

GLShader::GLShader(const string& filepath) :m_FilePath(filepath), m_RendererID(0)
{
	auto LastSlash = filepath.find_last_of("/");
	LastSlash = LastSlash == string::npos ? 0 : LastSlash + 1;
	auto LastDot = filepath.find_last_of(".");
	LastDot = LastSlash == string::npos ? filepath.length() : LastDot;
	auto count = LastDot - LastSlash;
	m_Name = filepath.substr(LastSlash, count);

	Build(filepath);
}

void GLShader::Build(const string& filepath)
{
	CompiledShader compiled = CompileShaderFile(filepath, ShaderTarget::OpenGL);
	if (!compiled.Success)
	{
		Error_Core("[{}]: shader build failed", m_Name);
		return;
	}

	uniform = std::move(compiled.Uniforms);
	m_Reflection = std::move(compiled.Reflection);

	auto& sources = compiled.Glsl;
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


GLShader::~GLShader()
{
	glDeleteProgram(m_RendererID);
}

void GLShader::Bind()const
{
	glUseProgram(m_RendererID);
}

void GLShader::UnBind()const
{
	glUseProgram(0);
}



unsigned int  GLShader::CompileShader(unsigned int type, const string& source)
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

void GLShader::CheckLinkStatus(unsigned int program) const
{
	int linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (linked == GL_TRUE)
		return;

	int length = 0;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
	string log(length > 0 ? length : 1, '\0');
	glGetProgramInfoLog(program, length, &length, log.data());
	Error_Core("Failed to link [{}]: {}", m_Name, log);
}

Ref<GLShader> GLShader::Create(const string& filepath, const string& name)
{
	return CreateRef<GLShader>(filepath, name);
}

Ref<GLShader> GLShader::Create(const string& filepath)
{
	return CreateRef<GLShader>(filepath);
}

unsigned int GLShader::CreateShader(const string& vertexShader, const string& fragmentShader)
{
	unsigned int program = glCreateProgram();
	unsigned int vs = CompileShader(GL_VERTEX_SHADER, vertexShader);
	unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShader);

	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	CheckLinkStatus(program);
	glValidateProgram(program);

	glDeleteShader(vs);
	glDeleteShader(fs);

	return program;
}

unsigned int GLShader::CreateComputeShader(const string& computeSource)
{
	unsigned int cs = CompileShader(GL_COMPUTE_SHADER, computeSource);
	if (cs == 0)
		return 0;

	unsigned int program = glCreateProgram();
	glAttachShader(program, cs);
	glLinkProgram(program);
	CheckLinkStatus(program);
	glValidateProgram(program);

	glDeleteShader(cs);

	return program;
}

void GLShader::DispatchCompute(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ) const
{
	glDispatchCompute(groupsX, groupsY, groupsZ);
}

Ref<GLShader> GLShader::CreateCompute(const string& filepath)
{
	return CreateRef<GLShader>(filepath);
}

namespace {

struct PushMemberType
{
	enum class Scalar { Float, Int, Uint, Unknown };
	Scalar scalar = Scalar::Unknown;
	int columns = 1;
	int rows = 1;
	uint32_t count = 1;
};

bool ParsePushMemberType(const std::string& type, PushMemberType& out)
{
	std::string base = type;
	out.count = 1;
	const size_t bracket = type.find('[');
	if (bracket != std::string::npos)
	{
		if (type.find('[', bracket + 1) != std::string::npos || type.back() != ']')
			return false;
		base = type.substr(0, bracket);
		const std::string inner = type.substr(bracket + 1, type.size() - bracket - 2);
		if (inner.empty())
			return false;
		out.count = static_cast<uint32_t>(std::stoul(inner));
	}

	auto scalarVector = [&](PushMemberType::Scalar scalar, const char* prefix) {
		const size_t n = std::strlen(prefix);
		if (base.compare(0, n, prefix) != 0)
			return false;
		out.scalar = scalar;
		out.columns = 1;
		out.rows = base.size() == n ? 1 : base[n] - '0';
		return out.rows >= 1 && out.rows <= 4 && base.size() <= n + 1;
	};
	if (scalarVector(PushMemberType::Scalar::Float, "float")
		|| scalarVector(PushMemberType::Scalar::Float, "vec")
		|| scalarVector(PushMemberType::Scalar::Int, "int")
		|| scalarVector(PushMemberType::Scalar::Int, "ivec")
		|| scalarVector(PushMemberType::Scalar::Uint, "uint")
		|| scalarVector(PushMemberType::Scalar::Uint, "uvec"))
		return out.scalar != PushMemberType::Scalar::Unknown;

	if (base.compare(0, 3, "mat") != 0)
		return false;
	const std::string rest = base.substr(3);
	const size_t split = rest.find('x');
	out.scalar = PushMemberType::Scalar::Float;
	if (split == std::string::npos)
	{
		if (rest.size() != 1)
			return false;
		out.columns = out.rows = rest[0] - '0';
	}
	else
	{
		if (split != 1 || rest.size() != 3)
			return false;
		out.columns = rest[0] - '0';
		out.rows = rest[2] - '0';
	}
	return out.columns >= 2 && out.columns <= 4 && out.rows >= 2 && out.rows <= 4;
}

void UploadMatrix(int location, int columns, int rows, const uint8_t* element)
{
	const bool padded = rows < 4;
	float packed[16];
	const float* source = reinterpret_cast<const float*>(element);
	if (padded)
	{
		for (int column = 0; column < columns; ++column)
			std::memcpy(packed + column * rows, element + column * 16, sizeof(float) * rows);
		source = packed;
	}

	if (columns == rows)
	{
		if (columns == 2) glUniformMatrix2fv(location, 1, GL_FALSE, source);
		else if (columns == 3) glUniformMatrix3fv(location, 1, GL_FALSE, source);
		else glUniformMatrix4fv(location, 1, GL_FALSE, source);
		return;
	}
	if (columns == 2 && rows == 3) glUniformMatrix2x3fv(location, 1, GL_FALSE, source);
	else if (columns == 2 && rows == 4) glUniformMatrix2x4fv(location, 1, GL_FALSE, source);
	else if (columns == 3 && rows == 2) glUniformMatrix3x2fv(location, 1, GL_FALSE, source);
	else if (columns == 3 && rows == 4) glUniformMatrix3x4fv(location, 1, GL_FALSE, source);
	else if (columns == 4 && rows == 2) glUniformMatrix4x2fv(location, 1, GL_FALSE, source);
	else if (columns == 4 && rows == 3) glUniformMatrix4x3fv(location, 1, GL_FALSE, source);
}

void UploadScalar(int location, PushMemberType::Scalar scalar, int rows, const uint8_t* element)
{
	if (scalar == PushMemberType::Scalar::Float)
	{
		const float* value = reinterpret_cast<const float*>(element);
		if (rows == 1) glUniform1fv(location, 1, value);
		else if (rows == 2) glUniform2fv(location, 1, value);
		else if (rows == 3) glUniform3fv(location, 1, value);
		else glUniform4fv(location, 1, value);
	}
	else if (scalar == PushMemberType::Scalar::Int)
	{
		const int* value = reinterpret_cast<const int*>(element);
		if (rows == 1) glUniform1iv(location, 1, value);
		else if (rows == 2) glUniform2iv(location, 1, value);
		else if (rows == 3) glUniform3iv(location, 1, value);
		else glUniform4iv(location, 1, value);
	}
	else
	{
		const unsigned int* value = reinterpret_cast<const unsigned int*>(element);
		if (rows == 1) glUniform1uiv(location, 1, value);
		else if (rows == 2) glUniform2uiv(location, 1, value);
		else if (rows == 3) glUniform3uiv(location, 1, value);
		else glUniform4uiv(location, 1, value);
	}
}

uint32_t ElementReadSize(const PushMemberType& type)
{
	if (type.columns > 1)
		return static_cast<uint32_t>(type.columns * (type.rows == 4 ? type.rows : 4) * sizeof(float));
	return static_cast<uint32_t>(type.rows * sizeof(uint32_t));
}

void WarnPushMember(const std::string& shader, const std::string& member, const std::string& type)
{
	static std::unordered_set<std::string> seen;
	if (!seen.insert(shader + ":" + member + ":" + type).second)
		return;
	Warn_Core("[{}]: push constant member '{}' ({}) is not a flat scalar, vector, or matrix",
		shader, member, type);
}

}

void GLShader::ApplyPushConstants(const void* data, uint32_t size) const
{
	if (!data || size == 0 || m_Reflection.PushConstants.empty() || m_RendererID == 0)
		return;

	Bind();
	const auto* bytes = static_cast<const uint8_t*>(data);
	for (const ShaderBlock& block : m_Reflection.PushConstants)
	{
		for (const ShaderBlockMember& member : block.Members)
		{
			PushMemberType type;
			if (!ParsePushMemberType(member.Type, type))
			{
				WarnPushMember(m_Name, member.Name, member.Type);
				continue;
			}

			const uint32_t stride = member.ArrayStride != 0
				? member.ArrayStride
				: (type.count > 1 && type.count != 0 ? member.Size / type.count : member.Size);
			const uint32_t readSize = ElementReadSize(type);
			if (stride < readSize)
				continue;

			for (uint32_t index = 0; index < type.count; ++index)
			{
				const uint32_t offset = member.Offset + index * stride;
				if (offset + readSize > size)
					break;

				std::string name = block.Name.empty() ? member.Name : block.Name + "." + member.Name;
				if (type.count > 1)
					name += "[" + std::to_string(index) + "]";

				const int location = GetUniformLocation(name);
				if (location < 0)
					continue;
				const uint8_t* element = bytes + offset;
				if (type.columns > 1)
					UploadMatrix(location, type.columns, type.rows, element);
				else
					UploadScalar(location, type.scalar, type.rows, element);
			}
		}
	}
}

void GLShader::SetUniform4f(const string& name, float v0, float v1, float v2, float v3)const
{
	glUniform4f(GetUniformLocation(name), v0, v1, v2, v3);
}

void GLShader::SetUniform1f(const string& name, float value)const
{
	glUniform1f(GetUniformLocation(name), value);
}

void GLShader::SetUniform1i(const string& name, int value)const
{
	glUniform1i(GetUniformLocation(name), value);
}

void GLShader::SetUniformMat4f(const string& name, const glm::mat4& mat4)const
{
	glUniformMatrix4fv(GetUniformLocation(name), 1, false, &mat4[0][0]);
}

void GLShader::SetUniformVec3(const string& name, const glm::vec3& value) const
{
	glUniform3fv(GetUniformLocation(name), 1, &value[0]);
}
void GLShader::SetUniformVec2(const string& name, const glm::vec2& value) const
{
	glUniform2fv(GetUniformLocation(name), 1, &value[0]);
}


int GLShader::GetUniformLocation(const string& name)  const
{
	if (m_UniformLocationCache.find(name) != m_UniformLocationCache.end())
		return m_UniformLocationCache[name];

	int locatation = glGetUniformLocation(m_RendererID, name.c_str());
	if (locatation == -1)
		Warn_Core("["+m_Name + "]:Warning: uniform '" + name + "' doesnt exist!");
	m_UniformLocationCache[name] = locatation;
	return locatation;
}

// --- RHI Factory methods (inline) ---
// Shader IS the GL implementation of RHIShader, so factories delegate to Shader::Create*.
#ifdef RenderAPI_OpenGL

// --- ShaderLibiray ---

shared_mutex ShaderLibiray::s_Mutex;
unordered_map<string, Ref<RHIShader>> ShaderLibiray::m_Shaders;

Ref<RHIShader> RHIShader::Create(const std::string& filepath)
{
	return GLShader::Create(filepath);
}

Ref<RHIShader> RHIShader::Create(
	const std::string& filepath,
	const std::string& name)
{
	return GLShader::Create(filepath, name);
}

Ref<RHIShader> RHIShader::CreateCompute(const std::string& filepath)
{
	return GLShader::CreateCompute(filepath);
}

#endif // RenderAPI_OpenGL
