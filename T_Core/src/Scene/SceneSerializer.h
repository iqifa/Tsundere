#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "Scene/Scene.h"
#include "Core/Assets/AssetDatabase.h"
#include "Core/MaterialLibrary.h"
#include "Platform/RHI/RHITextureLibrary.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

// Named text fields, like .tmat assets.
class SceneSerializer
{
    static std::string ResourcePath(const std::string& path)
    {
        if (path.empty()) return path;
        const std::filesystem::path requested(path);
        if (requested.is_absolute()) return requested.lexically_normal().string();
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        for (auto directory : { std::filesystem::path(executable).parent_path(), std::filesystem::current_path() })
        {
            while (!directory.empty())
            {
                const auto candidate = directory / requested;
                if (std::filesystem::exists(candidate)) return candidate.lexically_normal().string();
                const auto parent = directory.parent_path();
                if (parent == directory) break;
                directory = parent;
            }
        }
        throw std::runtime_error("Missing scene resource: " + path);
    }

    static void Field(std::istream& in, const char* expected)
    {
        std::string field;
        if (!(in >> field) || field != expected)
            throw std::runtime_error(std::string("Expected scene field: ") + expected);
    }

    static void WriteReference(std::ostream& out, const std::string& path, Engine::AssetKind kind)
    {
        const auto absolute = ResourcePath(path);
        uint64_t uid = 0;
        for (const auto& asset : Engine::AssetDatabase::GetByKind(kind))
            if (asset.path == absolute) { uid = asset.uid; break; }
        // Prefer project-relative paths; UID survives renames within Assets.
        std::filesystem::path stored(absolute);
        for (auto dir = stored.parent_path(); !dir.empty(); )
        {
            if (std::filesystem::is_directory(dir / "Assets") && std::filesystem::is_directory(dir / "res"))
            { stored = stored.lexically_relative(dir); break; }
            auto parent = dir.parent_path();
            if (parent == dir) break;
            dir = parent;
        }
        out << uid << ' ' << std::quoted(stored.generic_string()) << ' ';
    }

    static std::string ReadReference(std::istream& in, Engine::AssetKind kind)
    {
        uint64_t uid = 0;
        std::string path;
        in >> uid >> std::quoted(path);
        if (!in) throw std::runtime_error("Invalid asset reference");
        if (uid)
            for (const auto& asset : Engine::AssetDatabase::GetByKind(kind))
                if (asset.uid == uid) return asset.path;
        return ResourcePath(path);
    }

    static void WriteVector(std::ostream& out, const glm::vec3& value)
    {
        out << value.x << ' ' << value.y << ' ' << value.z << ' ';
    }

    static void ReadVector(std::istream& in, glm::vec3& value)
    {
        in >> value.x >> value.y >> value.z;
    }

    static size_t Count(std::istream& in)
    {
        size_t count = 0;
        if (!(in >> count) || count > 1000000)
            throw std::runtime_error("Invalid scene record count");
        return count;
    }

    static void WriteMaterial(std::ostream& out, const Ref<Engine::Material>& material)
    {
        out << bool(material) << ' ';
        if (!material) return;
        if (!material->GetAssetPath().empty())
        {
            out << "Asset ";
            WriteReference(out, material->GetAssetPath(), Engine::AssetKind::Material);
            return;
        }
        out << "Inline ";
        out << std::quoted(material->GetName()) << ' '
            << std::quoted(ResourcePath(material->GetAssetPath())) << ' '
            << std::quoted(material->GetShader() ? ResourcePath(material->GetShader()->GetPath()) : "") << ' '
            << material->GetParameters().size() << '\n';
        for (const auto& [name, param] : material->GetParameters())
        {
            out << std::quoted(name) << ' ' << int(param.type) << ' ';
            using Type = Engine::MaterialParameter::Type;
            switch (param.type)
            {
            case Type::Float: out << *param.Get<float>(); break;
            case Type::Int: out << *param.Get<int>(); break;
            case Type::Vec2: {
                auto value = *param.Get<glm::vec2>();
                out << value.x << ' ' << value.y;
                break;
            }
            case Type::Vec3: WriteVector(out, *param.Get<glm::vec3>()); break;
            case Type::Vec4: {
                auto value = *param.Get<glm::vec4>();
                out << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w;
                break;
            }
            case Type::Texture2D: {
                auto texture = *param.Get<Ref<RHITexture2D>>();
                out << std::quoted(texture ? ResourcePath(texture->GetPath()) : "");
                break;
            }
            default: throw std::runtime_error("Unsupported material parameter");
            }
            out << '\n';
        }
    }

    static Ref<Engine::Material> ReadMaterial(std::istream& in)
    {
        bool present = false;
        in >> present;
        if (!present) return nullptr;
        std::string storage;
        in >> storage;
        if (storage == "Asset")
        {
            const auto path = ReadReference(in, Engine::AssetKind::Material);
            auto material = Engine::MaterialLibrary::Get(path);
            if (!material) throw std::runtime_error("Cannot load scene material: " + path);
            return material;
        }
        if (storage != "Inline") throw std::runtime_error("Invalid material storage");
        std::string name, asset, shader;
        in >> std::quoted(name) >> std::quoted(asset) >> std::quoted(shader);
        if (!shader.empty() && !std::filesystem::exists(shader))
            throw std::runtime_error("Missing scene shader");
        auto material = CreateRef<Engine::Material>(name,
            shader.empty() ? nullptr : ShaderLibiray::Get(shader));
        material->SetAssetPath(asset);
        const size_t count = Count(in);
        for (size_t i = 0; i < count; ++i)
        {
            int type = -1;
            in >> std::quoted(name) >> type;
            using Type = Engine::MaterialParameter::Type;
            switch (Type(type))
            {
            case Type::Float: { float v = 0; in >> v; material->SetFloat(name, v); break; }
            case Type::Int: { int v = 0; in >> v; material->SetInt(name, v); break; }
            case Type::Vec2: { glm::vec2 v{}; in >> v.x >> v.y; material->SetVec2(name, v); break; }
            case Type::Vec3: { glm::vec3 v{}; ReadVector(in, v); material->SetVec3(name, v); break; }
            case Type::Vec4: { glm::vec4 v{}; in >> v.x >> v.y >> v.z >> v.w; material->SetVec4(name, v); break; }
            case Type::Texture2D: {
                std::string path;
                in >> std::quoted(path);
                auto texture = path.empty() ? nullptr : TextureLibiary::Get(path);
                if (!path.empty() && !texture) throw std::runtime_error("Missing scene texture");
                material->SetTexture(name, texture);
                break;
            }
            default: throw std::runtime_error("Invalid material parameter type");
            }
        }
        return material;
    }

public:
    static bool Save(Scene& scene, const std::string& path, std::string& error)
    {
        try
        {
            std::ostringstream out;
            out << std::setprecision(std::numeric_limits<float>::max_digits10);
            std::vector<entt::entity> entities;
            std::unordered_map<const Engine::Material*, size_t> materialIds;
            for (auto entity : scene.m_Registry.view<Component::ID>()) entities.push_back(entity);
            out << "# Tsundere Scene File\nVersion: 2\nEntities: " << entities.size() << '\n';
            for (auto entity : entities)
            {
                auto& registry = scene.m_Registry;
                auto& transform = registry.get<Component::Transform>(entity);
                auto parent = registry.get<Component::Parent>(entity).parent;
                out << "\n[Entity]\nID: " << uint64_t(registry.get<Component::ID>(entity).id) << ' '
                    << "\nName: " << std::quoted(registry.get<Component::Tag>(entity).tag) << "\nParent: "
                    << (parent != entt::null) << ' ';
                if (parent != entt::null) out << uint64_t(registry.get<Component::ID>(parent).id) << ' ';
                out << "\nPosition: "; WriteVector(out, transform.Position);
                out << "\nRotation: "; WriteVector(out, transform.Rotation);
                out << "\nScale: "; WriteVector(out, transform.Scale);
                const auto* light = registry.try_get<Component::Light>(entity);
                out << "\nLight: " << bool(light) << ' ';
                if (light)
                {
                    out << int(light->type) << ' ';
                    WriteVector(out, light->Color);
                    WriteVector(out, light->Direction);
                    out << light->Intensity << ' ' << light->Ambient << ' ';
                    if (light->type == Component::Light::LightType::Area)
                        out << light->Params.Area.Width << ' ' << light->Params.Area.Height << ' ' << light->Params.Area.Shape << ' ';
                    else
                        for (float value : light->Params.Data) out << value << ' ';
                }
                const auto* camera = registry.try_get<Component::Camera>(entity);
                out << "\nCamera: " << bool(camera) << ' ';
                if (camera) WriteVector(out, camera->Target);
                const auto* mesh = registry.try_get<Component::MeshRender>(entity);
                out << "\nMeshRender: " << bool(mesh) << ' ';
                if (mesh)
                {
                    out << "\nModel: ";
                    WriteReference(out, mesh->ModelPath, Engine::AssetKind::Model);
                    out << "\nMaterials: " << mesh->materials.size() << '\n';
                    for (const auto& material : mesh->materials)
                    {
                        auto [it, inserted] = materialIds.emplace(material.get(), materialIds.size());
                        out << "Material: " << it->second << ' ';
                        if (inserted) WriteMaterial(out, material);
                        out << '\n';
                    }
                }
                out << '\n';
            }
            // Write beside the destination, then atomically replace it on Windows.
            const std::filesystem::path target = std::filesystem::absolute(path);
            auto temporary = target;
            temporary += "." + std::to_string(uint64_t(UID())) + ".tmp";
            try
            {
                std::ofstream file(temporary, std::ios::binary);
                file << out.str();
                file.close();
                if (!file) throw std::runtime_error("Cannot write scene file");
                if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot replace scene file");
            }
            catch (...)
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                throw;
            }
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    static bool Load(Scene& scene, const std::string& path, std::string& error)
    {
        try
        {
            std::ifstream in(path, std::ios::binary);
            std::string signature;
            int version = 0;
            std::getline(in, signature);
            if (signature != "# Tsundere Scene File" && signature != "# Tsundere Scene File\r")
                throw std::runtime_error("Unsupported scene format");
            Field(in, "Version:");
            in >> version;
            if (version != 2) throw std::runtime_error("Unsupported scene version");
            Field(in, "Entities:");
            Scene loaded;
            std::unordered_map<uint64_t, entt::entity> entities;
            std::unordered_map<uint64_t, uint64_t> parents;
            std::vector<Ref<Engine::Material>> materialTable;
            const size_t count = Count(in);
            for (size_t i = 0; i < count; ++i)
            {
                uint64_t id = 0, parent = 0;
                std::string name;
                bool present = false;
                Field(in, "[Entity]");
                Field(in, "ID:"); in >> id;
                Field(in, "Name:"); in >> std::quoted(name);
                Field(in, "Parent:"); in >> present;
                if (present) { in >> parent; parents.emplace(id, parent); }
                if (entities.count(id)) throw std::runtime_error("Duplicate entity ID");
                auto entity = loaded.CreateEntityWithUID(UID(id), name);
                entities.emplace(id, entity);
                auto& transform = entity.GetComponent<Component::Transform>();
                Field(in, "Position:"); ReadVector(in, transform.Position);
                Field(in, "Rotation:"); ReadVector(in, transform.Rotation);
                Field(in, "Scale:"); ReadVector(in, transform.Scale);
                Field(in, "Light:"); in >> present;
                if (present)
                {
                    auto& light = entity.AddComponent<Component::Light>();
                    int type = -1;
                    in >> type;
                    if (type < 0 || type > 3) throw std::runtime_error("Invalid light type");
                    light.type = Component::Light::LightType(type);
                    ReadVector(in, light.Color);
                    ReadVector(in, light.Direction);
                    in >> light.Intensity >> light.Ambient;
                    if (light.type == Component::Light::LightType::Area)
                        in >> light.Params.Area.Width >> light.Params.Area.Height >> light.Params.Area.Shape;
                    else
                        for (float& value : light.Params.Data) in >> value;
                }
                Field(in, "Camera:"); in >> present;
                if (present) ReadVector(in, entity.AddComponent<Component::Camera>().Target);
                Field(in, "MeshRender:"); in >> present;
                if (present)
                {
                    auto& mesh = entity.AddComponent<Component::MeshRender>();
                    Field(in, "Model:");
                    mesh.ModelPath = ReadReference(in, Engine::AssetKind::Model);
                    if (!mesh.ModelPath.empty() && !std::filesystem::exists(mesh.ModelPath))
                        throw std::runtime_error("Missing scene model");
                    Field(in, "Materials:");
                    const size_t materials = Count(in);
                    for (size_t j = 0; j < materials; ++j)
                    {
                        Field(in, "Material:");
                        const size_t materialId = Count(in);
                        if (materialId > materialTable.size()) throw std::runtime_error("Invalid material reference");
                        if (materialId == materialTable.size()) materialTable.push_back(ReadMaterial(in));
                        mesh.materials.push_back(materialTable.at(materialId));
                    }
                }
                if (!in) throw std::runtime_error("Truncated or malformed scene");
            }
            in >> std::ws;
            if (!in.eof()) throw std::runtime_error("Unexpected scene data");
            for (const auto& [id, parent] : parents)
            {
                std::unordered_set<uint64_t> visited{ id };
                uint64_t ancestor = parent;
                for (;;)
                {
                    if (!entities.count(ancestor) || !visited.insert(ancestor).second)
                        throw std::runtime_error("Invalid scene hierarchy");
                    const auto it = parents.find(ancestor);
                    if (it == parents.end()) break;
                    ancestor = it->second;
                }
                Entity{ &loaded, entities.at(parent) }.addchildwithchangeparent(entities.at(id));
            }
            scene.m_Registry.swap(loaded.m_Registry);
            ++scene.Revision;
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }
};
