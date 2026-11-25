#include <memory>
#include "Core/Assets/CubemapAsset.h"
#include "SkyboxSettings.h"
#include <iostream>

static void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

// Minimal 24-bit BMP so CubemapAsset::Save can decode real faces.
static void WriteBmp(const std::filesystem::path& path, int size)
{
    const int row = (size * 3 + 3) & ~3;
    const uint32_t pixels = uint32_t(row * size), fileSize = 54 + pixels;
    unsigned char header[54] = { 'B', 'M' };
    auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) header[at + i] = (v >> (8 * i)) & 0xFF; };
    put32(2, fileSize); put32(10, 54); put32(14, 40); put32(18, size); put32(22, size);
    header[26] = 1; header[28] = 24; put32(34, pixels);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    const std::string data(pixels, char(0x80));
    file.write(data.data(), data.size());
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

        // Save replaces faces of an existing cubemap; invalid faces leave it untouched.
        {
            const auto dir = root / "Assets" / "save";
            fs::create_directories(dir);
            const auto a = dir / "a.bmp", b = dir / "b.bmp", big = dir / "big.bmp";
            WriteBmp(a, 2); WriteBmp(b, 2); WriteBmp(big, 4);
            Engine::AssetDatabase::Sync(root / "Assets");
            const auto saved = dir / "edit.tcubemap";
            std::array<std::string, 6> faces;
            faces.fill(a.string());
            Engine::CubemapAsset::Import(faces, saved.string());
            bool duplicate = false;
            try { Engine::CubemapAsset::Import(faces, saved.string()); }
            catch (const std::runtime_error&) { duplicate = true; }
            Require(duplicate, "Import overwrote an existing cubemap");
            faces[2] = b.string();
            Engine::CubemapAsset::Save(faces, saved.string());
            const auto uidA = Engine::AssetDatabase::UidForPath(a, Engine::AssetKind::Texture);
            const auto uidB = Engine::AssetDatabase::UidForPath(b, Engine::AssetKind::Texture);
            auto ids = Engine::CubemapAsset::ReadUids(saved.string());
            for (size_t i = 0; i < ids.size(); ++i)
                Require(ids[i] == (i == 2 ? uidB : uidA), "Save wrote wrong face IDs");
            faces[0] = big.string();
            bool mismatch = false;
            try { Engine::CubemapAsset::Save(faces, saved.string()); }
            catch (const std::runtime_error&) { mismatch = true; }
            Require(mismatch, "Mismatched face size accepted");
            Require(Engine::CubemapAsset::ReadUids(saved.string()) == ids, "Failed save changed the cubemap");
            bool gone = false;
            try { Engine::CubemapAsset::Save(faces, (dir / "none.tcubemap").string()); }
            catch (const std::runtime_error&) { gone = true; }
            Require(gone, "Save created a new cubemap");

            const auto cubeId = Engine::AssetDatabase::UidForPath(saved, Engine::AssetKind::Cubemap);
            Engine::AssetDatabase::Sync(root / "Assets");
            const auto cubeId2 = Engine::AssetDatabase::UidForPath(saved, Engine::AssetKind::Cubemap);
            Require(cubeId2 != 0 && (cubeId == 0 || cubeId == cubeId2), "Saved cubemap lost its ID");
            const auto before = Engine::AssetDatabase::ContentRevision(cubeId2);
            Engine::AssetDatabase::MarkContentChanged(cubeId2);
            Require(Engine::AssetDatabase::ContentRevision(cubeId2) == before + 1, "Content revision not bumped");
            Engine::AssetDatabase::Sync(root / "Assets");
            Require(Engine::AssetDatabase::ContentRevision(cubeId2) == before + 1, "Sync reset content revision");
            Require(Engine::AssetDatabase::ContentRevision(uidA) == 0, "Unrelated asset revision changed");
        }

        fs::remove_all(root);
        std::cout << "PASS: UUID reference moves, settings round trip, missing references, type checks, old format rejection, face save\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << " (fixtures: " << root << ")\n";
        return 1;
    }
}
