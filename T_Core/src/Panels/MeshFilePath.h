#pragma once
#ifndef MFP
#define MFP
#include"HeadLine.h"
#include"Scene/Modle.h"
#include"Scene/SceneRender.h"
extern std::vector<std::string> ModlePaths;
extern std::vector<std::string> ShaderPaths;
extern SceneRender sr;
class My_map
{
public:
	static std::unordered_map<std::string, Ref<Model>> m_ModleMap;
	static std::unordered_map<std::string, Ref<Shader>> m_ShaderMap;
};

#endif // !MFP