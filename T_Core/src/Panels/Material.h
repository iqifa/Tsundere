 #pragma once
#ifndef MATERIAL

#define MATERIAL

#include"GLHead.h"
#include"HeadLine.h"
#include"ExternalFiles.h"
#include <any>
enum class ValueType {
	NONE=-1,INT=0,FLOAT=1,DOUBLE=2,CHAR=3,STR=4,VEC3=5,VEC2=6,TEXTURE=7,HEADER=8
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
		InitVaires();
		
	}
	//vector<unsigned int> varies;

	//id,Type,Name
	vector<tuple<unsigned int,ValueType,string>> varies;

	void InitVaires()
	{
		for (auto var : varies)
		{
			free((void*)std::get<0>(var));
		}
		varies.clear();
		vector<Uniform> uniforms = shader->uniform;
		for (auto uniform : uniforms)
		{
			InitVarie(uniform);
		}
	}

	void InitVarie(Uniform uniform);
	void Render();
	void Save();
};

class MaterialLibiary {
	static unordered_map<string, Ref<Material>> mat_map;

	Ref<Material> Load(string path)
	{


		return CreateRef<Material>();
	}
};
#endif // !MATERIAL
