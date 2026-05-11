#include"MeshFilePath.h"
using namespace std;

static vector<string> ModlePaths = {
	"res/modle/nanosuit.obj",
	"res/cube/cube.obj",
	"res/Wood/WoodCrate.obj",
	"res/Wood/WoodCrate.fbx",
	"res/defaultmodle/default.obj",
	"D:/Download/Test/Test.fbx",
	"D:/Download/Test/build/Build.fbx",
	"D:/Download/Test/build/Build.obj",
};

static vector<string> ShaderPaths
{
	"res/shaders/Lit.shader",
	"res/shaders/default.shader",
	"res/shaders/Basic.shader",
	"res/shaders/Modle.shader",
	"res/shaders/SkyBox.shader",
	"res/shaders/Test.shader",
	"res/shaders/StoneShader.shader",
};

unordered_map<string, Ref<Model>> My_map::m_ModleMap;
unordered_map<string, Ref<Shader>> My_map::m_ShaderMap;

Ref<Model> My_map::LoadModel(const std::string& path)
{
	auto it = m_ModleMap.find(path);
	if (it != m_ModleMap.end())
	{
		debugwarring("Model: " + path + " already loaded");
		return it->second;
	}
	auto model = CreateRef<Model>(path);
	m_ModleMap[path] = model;
	return model;
}

Ref<Model> My_map::GetModel(const std::string& path)
{
	auto it = m_ModleMap.find(path);
	if (it != m_ModleMap.end())
		return it->second;
	return LoadModel(path);
}

const std::vector<std::string>& My_map::GetShaderPaths() {
	return ShaderPaths;
}
const std::vector<std::string>& My_map::GetModelPaths() {
	return ModlePaths;
}