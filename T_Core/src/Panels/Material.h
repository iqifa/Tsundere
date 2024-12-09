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



class Material
{
public:
	Ref<Shader> shader;
	int shaderindex = -1;
	Texture texture;
	Material(const Material&) = default;
	Material(const Ref<Shader>&shader) :shader(shader) {}
	Material(const Ref<Shader>&shader, const Texture & texture) :shader(shader), texture(texture) {}
	Material(const std::string & path = "res/shaders/default.sahder")
	{
		//shader = CreateRef<Shader>(path);
		shader = ShaderLibiray::Get(path);
		InitVaires();
		
	}
	//vector<unsigned int> varies;

	//id,Type,Name
	std::vector<std::tuple<unsigned int,ValueType, std::string>> varies;

	void InitVaires()
	{
		for (auto var : varies)
		{
			free((void*)std::get<0>(var));
		}
		varies.clear();
		std::vector<Uniform> uniforms = shader->uniform;
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
	static std::unordered_map<std::string, Ref<Material>> mat_map;

	Ref<Material> Load(std::string path)
	{


		return CreateRef<Material>();
	}
};
#endif // !MATERIAL
