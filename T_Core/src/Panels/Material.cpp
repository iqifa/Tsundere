#include"Material.h"
#include"Debug/Debug.h"
using namespace std;
template<typename T,typename... Args>
void ValueChange(string name, unsigned int a, Ref<RHIShader> shader,Args...args)
{

}
template<>
void ValueChange<int>(string name, unsigned int a, Ref<RHIShader> shader)
{
	int* value = (int*)a;
	shader->SetUniform1i(name, *value);
}
template<>
void ValueChange<Ref<RHITexture2D>>(string name, unsigned int a, Ref<RHIShader> shader,int count)
{
	Ref<RHITexture2D>* value = (Ref<RHITexture2D>*)a;

	// Find the binding for this sampler. With GLSL 420's `layout(binding=N)`,
	// the sampler reads exclusively from texture unit N — the `uniform1i`
	// setter is a no-op for those. The Material's job is to bind the texture
	// to the same unit the shader reads from, not to a running counter.
	//
	// `count` is kept as a fallback for older shaders that don't pin a
	// binding, but every modern shader pins via @binding or via the parser's
	// auto-assigned sampler range starting at 10.
	int slot = count;
	for (const auto& u : shader->GetUniforms())
	{
		if (u.Name == name && u.Type == "sampler2D")
		{
			if (u.binding > 0) { slot = (int)u.binding; }
			break;
		}
	}

	if (*value)
		(*value)->Bind(slot);
	else
	{
		RHIRenderer::GetCmd()->BindTexture2D(slot, 0);
	}
}

void Material::InitVarie(Uniform uniform)
{
	if (uniform.Type == "int")
	{
		int* value = new int;
		varies.push_back(tuple<unsigned int, ValueType, string>((unsigned int)value, ValueType::INT, uniform.Name));
	}
	else if (uniform.Type == "float")
	{
		float* value = new float;
		varies.push_back(tuple<unsigned int, ValueType, string>((unsigned int)value, ValueType::FLOAT, uniform.Name));
	}
	else if (uniform.Type == "double")
	{
		double* value = new double;
		varies.push_back(tuple<unsigned int, ValueType, string>((unsigned int)value, ValueType::DOUBLE, uniform.Name));
	}
	else if (uniform.Type == "char")
	{
		char* value = new char;
		varies.push_back(tuple<unsigned int, ValueType, string>((unsigned int)value, ValueType::CHAR, uniform.Name));
	}
	else if (uniform.Type == "vec2")
	{
		vec2* value = new vec2;
		varies.push_back(tuple<unsigned int, ValueType, string>((unsigned int)value, ValueType::VEC2, uniform.Name));
	}
	else if (uniform.Type == "vec3")
	{
		vec3* value = new vec3;
		varies.push_back(tuple<unsigned int, ValueType, string>((unsigned int)value, ValueType::VEC3, uniform.Name));
	}
	else if (uniform.Type == "sampler2D")
	{
		Ref<RHITexture2D>* value = new Ref<RHITexture2D>;
		varies.push_back(tuple<unsigned int, ValueType, string>((unsigned int)value, ValueType::TEXTURE, uniform.Name));
	}
	else if (uniform.Type == "Head")
	{
		varies.push_back(tuple<unsigned int, ValueType, string>(0, ValueType::HEADER, uniform.Name));
	}
}

void Material::Render(Ref<RHIShader> overrideShader)
{
	int count = 1;
	Ref<RHIShader> targetShader = overrideShader ? overrideShader : shader;
	targetShader->Bind();
	for (auto value : varies)
	{
		switch (std::get<1>(value))
		{
		case ValueType::INT:ValueChange<int>(std::get<2>(value), std::get<0>(value), targetShader);break;
		case ValueType::FLOAT: { float* v = (float*)std::get<0>(value); targetShader->SetUniform1f(std::get<2>(value), *v); break; }
		case ValueType::DOUBLE: { float v = (float)*(double*)std::get<0>(value); targetShader->SetUniform1f(std::get<2>(value), v); break; }
		case ValueType::VEC2: { vec2* v = (vec2*)std::get<0>(value); targetShader->SetUniformVec2(std::get<2>(value), *v); break; }
		case ValueType::VEC3: { vec3* v = (vec3*)std::get<0>(value); targetShader->SetUniformVec3(std::get<2>(value), *v); break; }
		case ValueType::TEXTURE:
			ValueChange<Ref<RHITexture2D>>(std::get<2>(value), std::get<0>(value), targetShader, count++); break;
		default:
			break;
		}
	}
}

void Material::Save()
{
	if (m_FilePath.empty()) {
		Warn_Core("Material::Save: no filepath set");
		return;
	}
	fstream fs;
	fs.open(m_FilePath, ios::out);
	fs << shader->GetPath() << endl;
	for (auto var : varies)
	{
		unsigned int value = std::get<0>(var);
		string lable = std::get<2>(var);
		switch (std::get<1>(var))
		{
		case ValueType::INT:	fs << "int" << endl << lable << endl << *(int*)value << endl; break;
		case ValueType::FLOAT:	fs << "float" << endl << lable << endl << *(float*)value << endl; break;
		case ValueType::DOUBLE:	fs << "double" << endl << lable << endl << *(double*)value << endl; break;
		case ValueType::VEC2: { vec2* v = (vec2*)value; fs << "vec2" << endl << lable << endl << v->x << " " << v->y << endl; break; }
		case ValueType::VEC3: { vec3* v = (vec3*)value; fs << "vec3" << endl << lable << endl << v->x << " " << v->y << " " << v->z << endl; break; }
		case ValueType::TEXTURE:{
			Ref<RHITexture2D>* t = (Ref<RHITexture2D>*)value;
			fs << "texture" << endl << lable << endl;
			if (*t) fs << (*t)->GetPath();
			fs << endl;
			break;
		}
		case ValueType::HEADER:
		default:
			break;
		}
	}
}


unordered_map<string, Ref<Material>>MaterialLibiary::mat_map;
