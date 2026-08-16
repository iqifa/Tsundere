#include"MeshFilePath.h"
#include"Core/Threading/ResourceLoader.h"
using namespace std;

static vector<string> ModlePaths = {
	"res/modle/nanosuit.obj",
	"res/cube/cube.obj",
	"res/Wood/WoodCrate.obj",
	"res/Wood/WoodCrate.fbx",
	"res/defaultmodle/default.obj",
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
	"D:/Code/C++/Tsundere/res/shaders/Lamber.shader",
};

unordered_map<string, Ref<Model>> My_map::m_ModleMap;
unordered_map<string, Ref<GLShader>> My_map::m_ShaderMap;
shared_mutex My_map::s_Mutex;

Ref<Model> My_map::LoadModel(const std::string& path)
{
	{
		shared_lock lock(s_Mutex);
		auto it = m_ModleMap.find(path);
		if (it != m_ModleMap.end())
		{
			Warn_Core("Model: " + path + " already loaded");
			return it->second;
		}
	}
	// Load without holding lock (heavy Assimp work)
	auto model = CreateRef<Model>(path);
	{
		unique_lock lock(s_Mutex);
		m_ModleMap[path] = model;
	}
	return model;
}

Ref<Model> My_map::GetModel(const std::string& path)
{
	{
		shared_lock lock(s_Mutex);
		auto it = m_ModleMap.find(path);
		if (it != m_ModleMap.end())
			return it->second;
	}
	// Trigger async load — model will be available in a few frames.
	// Dedup set in ResourceLoader prevents re-enqueuing every frame.
	Engine::ResourceLoader::RequestModelLoad(path);
	return nullptr;
}

const std::vector<std::string>& My_map::GetShaderPaths() {
	return ShaderPaths;
}
const std::vector<std::string>& My_map::GetModelPaths() {
	return ModlePaths;
}

void My_map::AddModelPath(const std::string& path)
{
	for (auto& p : ModlePaths)
		if (p == path) return;
	ModlePaths.push_back(path);
}

void My_map::RemoveModelPath(const std::string& path)
{
	auto it = std::find(ModlePaths.begin(), ModlePaths.end(), path);
	if (it != ModlePaths.end())
		ModlePaths.erase(it);
}
