#pragma once
#ifndef MATERIAL

#define MATERIAL

#include"GLHead.h"
#include"HeadLine.h"
#include"ExternalFiles.h"
#include <any>
enum class ValueType {
	NONE=-1,INT=0,FLOAT=1,DOUBLE=2,CHAR=3,STR=4,VEC3=5,VEC2=6,TEXTURE=7
};

//template<>
//void ValueChange<float>(string name, unsigned int a, Ref<Shader> shader)
//{
//	float* value = (float*)a;
//	shader->SetUniform1i("")
//}
//template<>
//void ValueChange<string>(string name, unsigned int a, Ref<Shader> shader)
//{
//	string* value = (string*)a;
//	char* str = value->data();
//	ImGui::InputText(name.c_str(), str, 100);
//	*value = str;
//}
//template<>
//void ValueChange<vec2>(string name, unsigned int a, Ref<Shader> shader)
//{
//	vec2* value = (vec2*)a;
//}
//template<>
//void ValueChange<vec3>(string name, unsigned int a, Ref<Shader> shader)
//{
//	vec3* value = (vec3*)a;
//	float* aa = (float*)value;
//	ImGui::InputFloat3(name.c_str(), aa);
//}
//template<>
//void ValueChange<double>(string name, unsigned int a, Ref<Shader> shader)
//{
//	double* value = (double*)a;
//	ImGui::InputDouble(name.c_str(), value);
//}

class Material
{
public:
	Ref<Shader> shader;
	int shaderindex = -1;
	Texture texture;
	Material(const Material&) = default;
	Material(const Ref<Shader>&shader) :shader(shader) {}
	Material(const Ref<Shader>&shader, const Texture & texture) :shader(shader), texture(texture) {}
	Material(const string & path = "res/shaders/default.sahder")
	{
		//shader = CreateRef<Shader>(path);
		shader = ShaderLibiray::Get(path);
		vector<Uniform> uniforms = shader->uniform;
		for (auto uniform : uniforms)
		{
			InitVaries(uniform);
		}
	}
	//vector<unsigned int> varies;
	vector<tuple<unsigned int,ValueType,string>> varies;
	void InitVaries(Uniform uniform) {
		if (uniform.Type == "int")
		{
			int* value = new int;
			varies.push_back(tuple<unsigned int, ValueType, string>((unsigned int)value, ValueType::INT,uniform.Name));
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
			int* value = new int;
			varies.push_back(tuple<unsigned int, ValueType, string>((unsigned int)value, ValueType::TEXTURE, uniform.Name));
		}
	}
	void Render(Ref<Shader> shader);
};




#endif // !MATERIAL
