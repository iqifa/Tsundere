#include"MeshFilePath.h"
vector<string> ModlePaths = {
	"res/modle/nanosuit.obj",
	"res/cube/cube.obj",
	"res/Wood/WoodCrate.obj",
	"res/Wood/WoodCrate.fbx",
	"res/defaultmodle/default.obj",
	"D:/Download/Test/Test.fbx",
	"D:/Download/Test/build/Build.fbx",
	"D:/Download/Test/build/Build.obj",
};
vector<string> ShaderPaths
{
	"res/shaders/default.shader",
	"res/shaders/Basic.shader",
	"res/shaders/Modle.shader",
	"res/shaders/SkyBox.shader",
	"res/shaders/Test.shader",
	"res/shaders/StoneShader.shader",
};
SceneRender sr;
unordered_map<string, Ref<Model>> My_map::m_ModleMap;
unordered_map<string, Ref<Shader>> My_map::m_ShaderMap;