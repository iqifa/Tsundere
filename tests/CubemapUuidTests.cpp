#include <memory>
#include "Core/Assets/CubemapAsset.h"
#include "SkyboxSettings.h"
#include <iostream>

static void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

int main()
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("tsundere-cubemap-" + std::to_string(GetCurrentProcessId()));
    try
    {
        Engine::Log::Init();
        fs::create_directories(root / "Assets");
        const auto texture = root / "Assets" / "face.png";
        std::ofstream(texture).put('x'); // Reference test; no image decoding.
        Engine::AssetDatabase::Sync(root / "Assets");
        const auto uid = Engine::AssetDatabase::UidForPath(texture, Engine::AssetKind::Texture);
        Require(uid != 0, "Texture ID was not registered");
        const auto cube = root / "Assets" / "sky.tcubemap";
        {
            std::ofstream file(cube);
            file << "TCUBEMAP_UID\n";
            for (int i = 0; i < 6; ++i) file << uid << '\n';
        }
        Engine::AssetDatabase::Sync(root / "Assets");
        const auto cubeUid = Engine::AssetDatabase::UidForPath(cube, Engine::AssetKind::Cubemap);
        const auto config = root / "ProjectSettings" / "Skybox.ini";
        SkyboxSettings::Save(config, cubeUid);
        Require(SkyboxSettings::Load(config) == cubeUid, "Settings ID round trip failed");
        fs::create_directories(root / "Assets" / "moved");
        const auto moved = root / "Assets" / "moved" / "renamed.png";
        fs::rename(texture, moved);
        fs::rename(texture.string() + ".meta", moved.string() + ".meta");
        const auto movedCube = root / "Assets" / "moved" / "renamed.tcubemap";
        fs::rename(cube, movedCube);
        fs::rename(cube.string() + ".meta", movedCube.string() + ".meta");
        Engine::AssetDatabase::Sync(root / "Assets");
        Require(Engine::AssetDatabase::Resolve(SkyboxSettings::Load(config), Engine::AssetKind::Cubemap) == movedCube.string(), "Cubemap rename broke settings reference");
        const auto faces = Engine::CubemapAsset::References(movedCube.string());
        for (const auto& face : faces) Require(face == moved.string(), "Texture move broke face reference");
        Require(Engine::AssetDatabase::Resolve(uid, Engine::AssetKind::Cubemap).empty(), "Wrong asset kind accepted");
        fs::remove(moved);
        Engine::AssetDatabase::Sync(root / "Assets");
        bool missing = false;
        try { Engine::CubemapAsset::References(movedCube.string()); }
        catch (const fs::filesystem_error&) { missing = true; }
        Require(missing, "Missing texture reference accepted");
        std::ofstream(movedCube, std::ios::trunc) << "cubemap old/path.png\n";
        bool rejected = false;
        try { Engine::CubemapAsset::References(movedCube.string()); }
        catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "Old cubemap format accepted");
        std::ofstream(config, std::ios::trunc) << "cubemap old/path.tcubemap\n";
        rejected = false;
        try { SkyboxSettings::Load(config); }
        catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "Old settings accepted");
        fs::remove_all(root);
        std::cout << "PASS: UUID reference moves, settings round trip, missing references, type checks, old format rejection\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << " (fixtures: " << root << ")\n";
        return 1;
    }
}
