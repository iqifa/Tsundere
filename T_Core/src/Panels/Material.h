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



class T_API Material
{
public:
	Ref<RHIShader> shader;
	int shaderindex = 0;
	Ref<RHITexture2D> texture;
	std::string m_FilePath;
	Material(const Material& other) : shader(other.shader), shaderindex(other.shaderindex), texture(other.texture), m_FilePath(other.m_FilePath) {
		for (auto& var : other.varies) {
			unsigned int oldPtr = std::get<0>(var);
			ValueType type = std::get<1>(var);
			std::string name = std::get<2>(var);
			switch (type) {
			case ValueType::INT:    { int* v = new int(*(int*)oldPtr); varies.push_back({ (unsigned int)v, type, name }); break; }
			case ValueType::FLOAT:  { float* v = new float(*(float*)oldPtr); varies.push_back({ (unsigned int)v, type, name }); break; }
			case ValueType::DOUBLE: { double* v = new double(*(double*)oldPtr); varies.push_back({ (unsigned int)v, type, name }); break; }
			case ValueType::CHAR:   { char* v = new char(*(char*)oldPtr); varies.push_back({ (unsigned int)v, type, name }); break; }
			case ValueType::VEC2:   { vec2* v = new vec2(*(vec2*)oldPtr); varies.push_back({ (unsigned int)v, type, name }); break; }
			case ValueType::VEC3:   { vec3* v = new vec3(*(vec3*)oldPtr); varies.push_back({ (unsigned int)v, type, name }); break; }
			case ValueType::TEXTURE:{ Ref<RHITexture2D>* v = new Ref<RHITexture2D>(*(Ref<RHITexture2D>*)oldPtr); varies.push_back({ (unsigned int)v, type, name }); break; }
			case ValueType::HEADER: varies.push_back({ 0, type, name }); break;
			default: break;
			}
		}
	}
	Material(const Ref<RHIShader>&shader) :shader(shader) {}
	Material(const Ref<RHIShader>&shader, const Ref<RHITexture2D>& texture) :shader(shader), texture(texture) {}
	Material(const std::string & path = "res/shaders/default.shader")
	{
		m_FilePath = path + ".mat";
			m_FilePath = path + ".mat";
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
		std::vector<Uniform> uniforms = shader->GetUniforms();
		for (auto uniform : uniforms)
		{
			InitVarie(uniform);
		}
	}

	void InitVarie(Uniform uniform);
	void Render(Ref<RHIShader> overrideShader = nullptr);
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
