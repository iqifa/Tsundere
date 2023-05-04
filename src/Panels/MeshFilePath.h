#pragma once
#ifndef MFP
#define MFP
#include"HeadLine.h"
#include"Scene/Modle.h"
#include"Scene/SceneRender.h"
extern vector<string> ModlePaths;
extern vector<string> ShaderPaths;
extern SceneRender sr;
class ModleMap
{
public:
	static unordered_map<string, Ref<Model>> m_ModleMap;
};
#endif // !MFP