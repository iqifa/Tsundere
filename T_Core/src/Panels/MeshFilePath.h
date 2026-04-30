#pragma once
#ifndef MFP
#define MFP
#include"HeadLine.h"
#include"Scene/Modle.h"
extern std::vector<std::string> ModlePaths;
class T_API My_map
{
public:
	static std::unordered_map<std::string, Ref<Model>> m_ModleMap;
	static std::unordered_map<std::string, Ref<Shader>> m_ShaderMap;


	static const std::vector<std::string>& My_map::GetShaderPaths();
};

#endif // !MFP