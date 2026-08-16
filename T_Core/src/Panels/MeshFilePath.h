#pragma once
#ifndef MFP
#define MFP
#include"HeadLine.h"
#include"Scene/Modle.h"
#include"Platform/GL/Shader.h"
#include<shared_mutex>


class T_API My_map
{
public:
	static std::unordered_map<std::string, Ref<Model>> m_ModleMap;
	static std::unordered_map<std::string, Ref<GLShader>> m_ShaderMap;

	static Ref<Model> LoadModel(const std::string& path);
	static Ref<Model> GetModel(const std::string& path);

	static void AddModelPath(const std::string& path);
	static void RemoveModelPath(const std::string& path);

	static const std::vector<std::string>& GetShaderPaths();
	static const std::vector<std::string>& GetModelPaths();

	static std::shared_mutex s_Mutex;
};

#endif // !MFP
