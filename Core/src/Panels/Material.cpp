#include"Material.h"
template<typename T>
void ValueChange(string name, unsigned int a, Ref<Shader> shader)
{

}
template<>
void ValueChange<int>(string name, unsigned int a, Ref<Shader> shader)
{
	int* value = (int*)a;
	shader->SetUniform1i(name, *value);
}
template<>
void ValueChange<Texture>(string name, unsigned int a, Ref<Shader> shader)
{
	Texture* value = (Texture*)a;
	shader->SetUniform1i(name, value->GetTextureID());
}
void Material::Render(Ref<Shader> shader)
{
	shader->Bind();
	for (auto value : varies)
	{
		switch (std::get<1>(value))
		{
		case ValueType::INT:ValueChange<int>(std::get<2>(value), std::get<0>(value), shader);break;
		case ValueType::FLOAT:	  break;
		case ValueType::DOUBLE:	  break;
		case ValueType::VEC2:	  break;
		case ValueType::VEC3:	  break;
		case ValueType::TEXTURE:ValueChange<Texture>(std::get<2>(value), std::get<0>(value), shader); break;;
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
		case ValueType::INT:	fs << "int" << endl << lable << endl << (int*)value << endl;
		case ValueType::FLOAT:	fs << "float" << endl << lable << endl << (float*)value << endl;
		case ValueType::DOUBLE:	fs << "double" << endl << lable << endl << (double*)value << endl;
		case ValueType::VEC2:	fs << "vec2" << endl << lable << endl << (vec2*)value << endl;
		case ValueType::VEC3:	fs << "vec3" << endl << lable << endl << (vec3*)value << endl;
		case ValueType::TEXTURE:fs << "texture" << endl << lable << endl << (Texture*)value << endl;
		case ValueType::HEADER:
		default:
			break;
		}
	}
}
