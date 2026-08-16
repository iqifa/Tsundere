#pragma once
#include <cstdint>
#include <functional>

// ---------------------------------------------------------------------------
// AssetHandle<Tag> — 类型安全的不透明资产ID
//
// 用法：
//   using TextureHandle = AssetHandle<TextureTag>;
//   TextureHandle h = assetMgr.RequestTexture("path/to/tex.png");
//   if (h.IsValid()) { ... }
//
// 优于直接传 Ref<Texture> 的原因：
//   - 轻量（8 字节），可序列化，可存入 ECS 组件
//   - 不延长资源生命周期（不是 shared_ptr）
//   - 类型安全，TextureHandle 无法传给需要 MeshHandle 的接口
// ---------------------------------------------------------------------------

template<typename Tag>
struct AssetHandle {
    uint64_t id = 0;

    bool IsValid() const { return id != 0; }

    bool operator==(const AssetHandle& o) const { return id == o.id; }
    bool operator!=(const AssetHandle& o) const { return id != o.id; }
    bool operator< (const AssetHandle& o) const { return id <  o.id; }

    static AssetHandle Invalid() { return AssetHandle{0}; }
};

// Tag types — 一个 tag 对应一种资产类型
struct TextureTag  {};
struct MeshTag     {};
struct ModelTag    {};
struct ShaderTag   {};
struct MaterialTag {};

using TextureHandle  = AssetHandle<TextureTag>;
using MeshHandle     = AssetHandle<MeshTag>;
using ModelHandle    = AssetHandle<ModelTag>;
using ShaderHandle   = AssetHandle<ShaderTag>;
using MaterialHandle = AssetHandle<MaterialTag>;

// std::hash 特化，使 AssetHandle 可用于 unordered_map/unordered_set
namespace std {
    template<typename Tag>
    struct hash<AssetHandle<Tag>> {
        size_t operator()(const AssetHandle<Tag>& h) const noexcept {
            return std::hash<uint64_t>{}(h.id);
        }
    };
}
