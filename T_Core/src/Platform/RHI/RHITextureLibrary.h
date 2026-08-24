#pragma once

// Path-based texture cache (RHI-typed). Moved here from Platform/GL/Texture.h
// when the legacy Texture class was removed; the value type is now Ref<RHITexture2D>.

#include "Platform/RHI/RHITexture.h"
#include <unordered_map>
#include <shared_mutex>

class TextureLibiary
{
public:
    inline static std::unordered_map<std::string, Ref<RHITexture2D>> m_TextureMap;
    inline static std::shared_mutex s_Mutex;

    static void Add(Ref<RHITexture2D> tex)
    {
        std::unique_lock lock(s_Mutex);
        auto& path = tex->GetPath();
        m_TextureMap[path] = tex;
    }

    static Ref<RHITexture2D> Load(const std::string& filepath)
    {
        {
            std::shared_lock lock(s_Mutex);
            auto it = m_TextureMap.find(filepath);
            if (it != m_TextureMap.end())
                return it->second;
        }
        Texture2DDesc desc;
        desc.filePath = filepath;
        auto tex = RHITexture2D::Create(desc);
        Add(tex);
        return tex;
    }

    static Ref<RHITexture2D> Get(const std::string& filepath)
    {
        {
            std::shared_lock lock(s_Mutex);
            auto it = m_TextureMap.find(filepath);
            if (it != m_TextureMap.end())
                return it->second;
        }
        return Load(filepath);
    }
};
