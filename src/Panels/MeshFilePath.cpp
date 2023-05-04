#include"MeshFilePath.h"
vector<string> ModlePaths = {
	"res/modle/nanosuit.obj",
	"res/cube/cube.obj"
};
vector<string> ShaderPaths
{
	"res/shaders/default.shader",
	"res/shaders/Basic.shader",
	"res/shaders/Modle.shader",
	"res/shaders/SkyBox.shader",
	"res/shaders/Test.shader",
};
SceneRender sr;
unordered_map<string, Ref<Model>> ModleMap::m_ModleMap;