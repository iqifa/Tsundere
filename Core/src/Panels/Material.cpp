#include"Material.h"
template<typename T>
void ValueChange(string name, unsigned int a, Ref<Shader> shader)
{

}
template<>
void ValueChange<int>(string name, unsigned int a, Ref<Shader> shader)
{
	int* value = (int*)a;
	shader->SetUniform1i("texturesize", *value);
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
		default:
			break;
		}
	}
}
