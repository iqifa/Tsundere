#include "Scene/SceneSerializer.h"
#include "Scene/SceneRaycast.h"
#include "Scene/BVHBuilder.h"
#include "Scene/LocalLights.h"
#include <cassert>
#include <iostream>

int main()
{
    Engine::Log::Init();
    Scene scene;
    auto parent = scene.CreateEntity("Parent");
    auto child = scene.CreateEntity("Child");
    parent.addchildwithchangeparent(child);
    assert(scene.IsActive(child));
    parent.GetComponent<Component::Tag>().Active = false;
    assert(!scene.IsActive(parent) && !scene.IsActive(child));
    assert(child.GetComponent<Component::Tag>().Active);
    parent.GetComponent<Component::Tag>().Active = true;
    assert(scene.IsActive(child));
    child.GetComponent<Component::Tag>().Active = false;
    assert(!scene.IsActive(child) && scene.IsActive(parent));
    child.GetComponent<Component::Tag>().Active = true;

    auto& local = child.AddComponent<Component::Light>();
    local.type = Component::Light::LightType::Point;
    local.ResetParams();
    local.Intensity = 3;
    child.GetComponent<Component::Transform>().Position = {1, 2, 3};
    auto lights = GatherLocalLights(scene);
    assert(lights.size() == 1 && lights[0].positionType == glm::vec4(1, 2, 3, 1));
    assert(lights[0].colorIntensity.w == 3 && lights[0].params.x == 5);
    parent.GetComponent<Component::Tag>().Active = false;
    assert(GatherLocalLights(scene).empty());
    parent.GetComponent<Component::Tag>().Active = true;
    local.type = Component::Light::LightType::Spot;
    local.ResetParams();
    local.Params.Spot.InnerAngle = 12;
    local.Params.Spot.OuterAngle = 28;
    lights = GatherLocalLights(scene);
    assert(lights.size() == 1 && lights[0].positionType.w == 2);
    assert(lights[0].params.z == 12 && lights[0].params.w == 28);
    local.type = Component::Light::LightType::Area;
    local.ResetParams();
    local.Params.Area.Width = 4;
    local.Params.Area.Height = 2;
    local.Params.Area.Shape = 1;
    lights = GatherLocalLights(scene);
    assert(lights.size() == 1 && lights[0].positionType.w == 3);
    assert(lights[0].params == glm::vec4(4, 2, 1, 0));
    parent.GetComponent<Component::Tag>().Active = false;
    assert(GatherLocalLights(scene).empty());
    parent.GetComponent<Component::Tag>().Active = true;
    child.GetComponent<Component::Tag>().Active = false;
    assert(GatherLocalLights(scene).empty());
    child.GetComponent<Component::Tag>().Active = true;
    child.RemoveComponent<Component::Light>();
    child.GetComponent<Component::Transform>().Position = {};
    assert(GatherLocalLights(scene).empty());

    auto model = CreateRef<Model>();
    for (float z : { 0.0f, -1.0f })
    {
        Mesh mesh;
        mesh.vertices.resize(3);
        mesh.vertices[0].Position = { -1, -1, z };
        mesh.vertices[1].Position = { 1, -1, z };
        mesh.vertices[2].Position = { 0, 1, z };
        mesh.indices = { 0, 1, 2 };
        model->meshes.push_back(std::move(mesh));
    }
    const std::string key = "activation-test-model";
    My_map::m_ModleMap[key] = model;
    child.AddComponent<Component::MeshRender>().ModelPath = key;
    const auto cast = [&] { return SceneRaycast::Cast(scene, { 0, 0, 2 }, { 0, 0, -1 }, 10); };
    BVHBuilder builder;
    const auto instances = [&] {
        builder.GatherScene(scene);
        builder.BuildBVH();
        return builder.GetInstanceCount();
    };
    assert(cast() && cast()->distance == 2 && instances() == 2);
    model->meshes[0].Active = false;
    assert(cast() && cast()->distance == 3 && instances() == 1);
    model->meshes[1].Active = false;
    assert(!cast() && instances() == 0);
    model->meshes[0].Active = true;
    parent.GetComponent<Component::Tag>().Active = false;
    assert(!cast() && instances() == 0);
    parent.GetComponent<Component::Tag>().Active = true;
    assert(cast() && instances() == 1);

    child.RemoveComponent<Component::MeshRender>();
    parent.GetComponent<Component::Tag>().Active = false;
    std::filesystem::create_directories("build/activation-tests");
    const std::filesystem::path modelPath = "build/activation-tests/model.obj";
    const auto movedPath = modelPath.parent_path() / "renamed.obj";
    std::filesystem::remove(movedPath);
    std::filesystem::remove(movedPath.string() + ".meta");
    { std::ofstream file(modelPath); file << "# metadata fixture\n"; }
    { std::ofstream file(modelPath.string() + ".meta"); file << "uid: 123456\ntype: Model\ncustom: keep\n"; }
    std::string metadataError;
    assert(Engine::AssetDatabase::SetSubmeshActive(modelPath, 2, false, metadataError));
    assert(Engine::AssetDatabase::SetSubmeshActive(modelPath, 7, false, metadataError));
    assert(Engine::AssetDatabase::DisabledSubmeshes(modelPath) == std::vector<uint32_t>({2, 7}));
    assert(Engine::AssetDatabase::SetSubmeshActive(modelPath, 2, true, metadataError));
    Engine::AssetDatabase::Sync(modelPath.parent_path());
    assert(Engine::AssetDatabase::DisabledSubmeshes(modelPath) == std::vector<uint32_t>({7}));
    assert(Engine::AssetDatabase::UidForPath(modelPath, Engine::AssetKind::Model) == 123456);
    std::ifstream metadata(modelPath.string() + ".meta");
    std::string text((std::istreambuf_iterator<char>(metadata)), {});
    assert(text.find("custom: keep") != std::string::npos);
    metadata.close();
    std::filesystem::rename(modelPath, movedPath);
    std::filesystem::rename(modelPath.string() + ".meta", movedPath.string() + ".meta");
    Engine::AssetDatabase::Sync(modelPath.parent_path());
    assert(Engine::AssetDatabase::DisabledSubmeshes(movedPath) == std::vector<uint32_t>({7}));
    assert(!Engine::AssetDatabase::SetSubmeshActive(modelPath, 0, false, metadataError));
    assert(!metadataError.empty());
    const std::string path = "build/activation-tests/state.tscene";
    std::string error;
    assert(SceneSerializer::Save(scene, path, error));
    Scene loaded;
    assert(SceneSerializer::Load(loaded, path, error));
    for (auto id : loaded.m_Registry.view<Component::Tag>()) assert(!loaded.IsActive(id));
    // Reject version 2 without replacing the loaded scene.
    std::ifstream saved(path);
    std::ostringstream legacy;
    for (std::string line; std::getline(saved, line); )
    {
        if (line.rfind("Active:", 0) == 0) continue;
        legacy << (line == "Version: 3" ? "Version: 2" : line) << '\n';
    }
    const std::string legacyPath = "build/activation-tests/legacy.tscene";
    { std::ofstream file(legacyPath); file << legacy.str(); }
    const auto revision = loaded.Revision;
    assert(!SceneSerializer::Load(loaded, legacyPath, error));
    assert(error == "Unsupported scene version: 2 (expected 3)");
    assert(loaded.Revision == revision);
    assert(loaded.m_Registry.view<Component::Tag>().size() == 2);
    for (auto id : loaded.m_Registry.view<Component::Tag>()) assert(!loaded.IsActive(id));
    parent.GetComponent<Component::Parent>().parent = child;
    parent.GetComponent<Component::Tag>().Active = true;
    assert(!scene.IsActive(child)); // Defensive cycle handling.
    My_map::m_ModleMap.erase(key);
    std::cout << "PASS: activation, hierarchy, submesh picking/BVH, scene roundtrip, version rejection, model metadata persistence/rename\n";
}
