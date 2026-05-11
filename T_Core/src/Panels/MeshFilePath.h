#pragma once
#ifndef MFP
#define MFP
#include"HeadLine.h"
#include"Scene/Modle.h"


class T_API My_map
{
public:
	static std::unordered_map<std::string, Ref<Model>> m_ModleMap;
	static std::unordered_map<std::string, Ref<Shader>> m_ShaderMap;

	static Ref<Model> LoadModel(const std::string& path);
	static Ref<Model> GetModel(const std::string& path);

	static const std::vector<std::string>& GetShaderPaths();
	static const std::vector<std::string>& GetModelPaths();
};

#endif // !MFP
