#include "Scene/SceneSerializer.h"
#include "Scene/SceneRaycast.h"
#include "Scene/BVHBuilder.h"
#include "Scene/LocalLights.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <glm/gtc/constants.hpp>

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
    // Direction follows Transform rotation: zero rotation points along local -Y.
    {
        const auto approx = [](glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 1e-4f; };
        auto& rotation = child.GetComponent<Component::Transform>().Rotation;
        assert(approx(glm::vec3(lights[0].directionAmbient), glm::vec3(0, -1, 0)));
        rotation = {0, 0, glm::half_pi<float>()};   // Rz(90) maps -Y to +X
        assert(approx(glm::vec3(GatherLocalLights(scene)[0].directionAmbient), glm::vec3(1, 0, 0)));
        for (glm::vec3 d : {glm::vec3(0.6f, -0.8f, 0), glm::vec3(-0.5f, -1, -0.5f), glm::vec3(0, 1, 0),
                 glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), glm::vec3(0.3f, 0.2f, -0.9f)})
        {
            rotation = Component::LightRotationFromDirection(d);
            assert(approx(Component::LightDirection(child.GetComponent<Component::Transform>()), glm::normalize(d)));
        }
        rotation = {};
    }
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
    child.GetComponent<Component::Tag>().Static = true;
    assert(SceneSerializer::Save(scene, path, error));
    Scene loaded;
    assert(SceneSerializer::Load(loaded, path, error));
    for (auto id : loaded.m_Registry.view<Component::Tag>()) assert(!loaded.IsActive(id));
    // Static round-trips (version 4).
    int staticCount = 0;
    for (auto id : loaded.m_Registry.view<Component::Tag>())
    {
        const auto& tag = loaded.m_Registry.get<Component::Tag>(id);
        staticCount += tag.Static;
        assert(tag.Static == (tag.tag == "Child"));
    }
    assert(staticCount == 1);
    const auto rewrite = [&](const std::string& target, auto&& transformLine)
    {
        std::ifstream saved(path);
        std::ostringstream text;
        for (std::string line; std::getline(saved, line); )
            if (transformLine(line))
                text << line << '\n';
        std::ofstream file(target);
        file << text.str();
    };
    // Version 3 files (no Static field) still load, with Static = false.
    const std::string v3Path = "build/activation-tests/v3.tscene";
    rewrite(v3Path, [](std::string& line) {
        if (line.rfind("Static:", 0) == 0) return false;
        if (line == "Version: 5") line = "Version: 3";
        return true;
    });
    {
        Scene v3;
        assert(SceneSerializer::Load(v3, v3Path, error));
        assert(v3.m_Registry.view<Component::Tag>().size() == 2);
        for (auto id : v3.m_Registry.view<Component::Tag>())
            assert(!v3.m_Registry.get<Component::Tag>(id).Static);
    }
    // Version 4 lights stored a direction vector; loading moves it into Transform rotation.
    {
        Scene lit;
        auto lamp = lit.CreateEntity("Lamp");
        auto& spot = lamp.AddComponent<Component::Light>();
        spot.type = Component::Light::LightType::Spot;
        spot.ResetParams();
        lamp.GetComponent<Component::Transform>().Rotation = {0.4f, 0.0f, 0.0f};
        const std::string litPath = "build/activation-tests/lit.tscene";
        assert(SceneSerializer::Save(lit, litPath, error));
        Scene roundTrip;
        assert(SceneSerializer::Load(roundTrip, litPath, error));
        for (auto [id, t, l] : roundTrip.m_Registry.view<Component::Transform, Component::Light>().each())
            assert(t.Rotation == glm::vec3(0.4f, 0.0f, 0.0f));
        const std::string v4Path = "build/activation-tests/v4-light.tscene";
        std::ifstream saved(litPath);
        std::ostringstream text;
        for (std::string line; std::getline(saved, line); )
        {
            if (line == "Version: 5") line = "Version: 4";
            if (line.rfind("Light: 1 ", 0) == 0)
            {
                std::istringstream tokens(line);
                std::vector<std::string> parts;
                for (std::string token; tokens >> token; ) parts.push_back(token);
                // "Light:" present type r g b | direction | intensity ambient params...
                parts.insert(parts.begin() + 6, {"0.6", "-0.8", "0"});
                line.clear();
                for (const auto& part : parts) line += part + ' ';
            }
            text << line << "\n";
        }
        saved.close();
        std::ofstream(v4Path) << text.str();
        Scene v4;
        assert(SceneSerializer::Load(v4, v4Path, error));
        int lampCount = 0;
        for (auto [id, t, l] : v4.m_Registry.view<Component::Transform, Component::Light>().each())
        {
            ++lampCount;
            assert(l.type == Component::Light::LightType::Spot && l.Params.Spot.OuterAngle == spot.Params.Spot.OuterAngle);
            assert(glm::length(Component::LightDirection(t) - glm::vec3(0.6f, -0.8f, 0.0f)) < 1e-4f);
        }
        assert(lampCount == 1);
    }
    // Reject version 2 without replacing the loaded scene.
    const std::string legacyPath = "build/activation-tests/legacy.tscene";
    rewrite(legacyPath, [](std::string& line) {
        if (line.rfind("Active:", 0) == 0 || line.rfind("Static:", 0) == 0) return false;
        if (line == "Version: 5") line = "Version: 2";
        return true;
    });
    const auto revision = loaded.Revision;
    assert(!SceneSerializer::Load(loaded, legacyPath, error));
    assert(error == "Unsupported scene version: 2 (expected 3 to 5)");
    assert(loaded.Revision == revision);
    assert(loaded.m_Registry.view<Component::Tag>().size() == 2);
    for (auto id : loaded.m_Registry.view<Component::Tag>()) assert(!loaded.IsActive(id));
    parent.GetComponent<Component::Parent>().parent = child;
    parent.GetComponent<Component::Tag>().Active = true;
    assert(!scene.IsActive(child)); // Defensive cycle handling.
    My_map::m_ModleMap.erase(key);
    std::cout << "PASS: activation, hierarchy, submesh picking/BVH, scene roundtrip, version rejection, model metadata persistence/rename\n";
}
