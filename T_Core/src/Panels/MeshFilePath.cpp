#include"MeshFilePath.h"
using namespace std;
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
static vector<string> ShaderPaths
{
	"D:/Code/C++/Tsundere/res/shaders/Lit.shader",
	"res/shaders/default.shader",
	"res/shaders/Basic.shader",
	"res/shaders/Modle.shader",
	"res/shaders/SkyBox.shader",
	"res/shaders/Test.shader",
	"res/shaders/StoneShader.shader",
};
unordered_map<string, Ref<Model>> My_map::m_ModleMap;
unordered_map<string, Ref<Shader>> My_map::m_ShaderMap;

const std::vector<std::string>& My_map::GetShaderPaths() {
	return ShaderPaths;
}