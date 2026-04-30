#include"Material.h"
using namespace std;
template<typename T,typename... Args>
void ValueChange(string name, unsigned int a, Ref<Shader> shader,Args...args)
{

}
template<>
void ValueChange<int>(string name, unsigned int a, Ref<Shader> shader)
{
	int* value = (int*)a;
	shader->SetUniform1i(name, *value);
}
template<>
void ValueChange<Texture>(string name, unsigned int a, Ref<Shader> shader,int count)
{
	/*Texture* value = (Texture*)a;
	shader->SetUniform1i(name, count);
	glBindTexture(GL_TEXTURE_2D, value->GetTextureID());*/
	glActiveTexture(GL_TEXTURE0 + count);
	Texture* value = (Texture*)a;
	shader->SetUniform1i(name, count);
	glBindTexture(GL_TEXTURE_2D, value->GetTextureID());
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
		Texture* value = new Texture;
		varies.push_back(tuple<unsigned int, ValueType, string>((unsigned int)value, ValueType::TEXTURE, uniform.Name));
	}
	else if (uniform.Type == "Head")
	{
		varies.push_back(tuple<unsigned int, ValueType, string>(0, ValueType::HEADER, uniform.Name));
	}
}

void Material::Render()
{
	int count = 1;
	shader->Bind();
	for (auto value : varies)
	{
		switch (std::get<1>(value))
		{
		case ValueType::INT:ValueChange<int>(std::get<2>(value), std::get<0>(value), shader);break;
		case ValueType::FLOAT: { float* v = (float*)std::get<0>(value); shader->SetUniform1f(std::get<2>(value), *v); break; }
		case ValueType::DOUBLE: { float v = (float)*(double*)std::get<0>(value); shader->SetUniform1f(std::get<2>(value), v); break; }
		case ValueType::VEC2: { vec2* v = (vec2*)std::get<0>(value); shader->SetUniformVec2(std::get<2>(value), *v); break; }
		case ValueType::VEC3: { vec3* v = (vec3*)std::get<0>(value); shader->SetUniformVec3(std::get<2>(value), *v); break; }
		case ValueType::TEXTURE:
			ValueChange<Texture>(std::get<2>(value), std::get<0>(value), shader, count++); break;
		default:
			break;
		}
	}
}

void Material::Save()
{
	string filepath;

	fstream fs;
	fs.open(filepath, ios::out);
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
		case ValueType::TEXTURE:{ Texture* t = (Texture*)value; fs << "texture" << endl << lable << endl << t->GetPath() << endl; break; }
		case ValueType::HEADER:
		default:
			break;
		}
	}
}


unordered_map<string, Ref<Material>>MaterialLibiary::mat_map;
