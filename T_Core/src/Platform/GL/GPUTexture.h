#pragma once
#include "Renderer.h"
#include "HeadLine.h"
#include "Core/Assets/TextureAsset.h"
#include "Core/Assets/GPUDeletionQueue.h"
#include "Debug/Debug.h"
#include <string>
#include <unordered_map>
#include <shared_mutex>

// ---------------------------------------------------------------------------
// GPUTexture — OpenGL 贴图对象（仅 GL 状态，无 CPU 像素数据）
//
// 创建/销毁必须在主 GL 线程。
// 如果 shared_ptr 的最后一个引用在非 GL 线程释放，
// 析构函数会把 glDeleteTextures 请求放入 GPUDeletionQueue，
// 主线程在下帧 Flush() 时统一执行。
// ---------------------------------------------------------------------------
class T_API GPUTexture {
public:
    GPUTexture() = default;
    ~GPUTexture();

    // 不可拷贝（GL 对象唯一归属）
    GPUTexture(const GPUTexture&)            = delete;
    GPUTexture& operator=(const GPUTexture&) = delete;
    // 可移动
    GPUTexture(GPUTexture&& o) noexcept;
    GPUTexture& operator=(GPUTexture&& o) noexcept;

    void Bind(unsigned int slot = 0) const;
    void UnBind() const;

    unsigned int GetID()      const { return m_ID; }
    // 兼容旧 Texture 接口名
    unsigned int GetTextureID()     { return m_ID; }
    int          GetWidth()   const { return m_Width; }
    int          GetHeight()  const { return m_Height; }
    std::string  GetPath()    const { return m_Path; }
    bool         IsValid()    const { return m_ID != 0; }

    // 预估显存占用（RGBA8）
    size_t GPUSize() const;

    // ── 工厂：从已解码的 CPU 数据上传 GPU（必须在主 GL 线程调用） ──
    static Ref<GPUTexture> CreateFromAsset(const TextureAsset& asset);

    // ── 兼容旧接口：从裸指针创建（兼容 ResourceLoader 过渡期） ──
    static Ref<GPUTexture> CreateFromPixels(const std::string& path,
        unsigned char* pixelData, int width, int height, int channels);

private:
    unsigned int m_ID       = 0;
    int          m_Width    = 0;
    int          m_Height   = 0;
    int          m_Channels = 0;
    std::string  m_Path;

    // 内部：设置纹理参数并上传像素
    void Upload(const unsigned char* pixels, int w, int h, int channels);
};

// ---------------------------------------------------------------------------
// GPUTextureLibrary — GPU 贴图缓存
// 替代旧的 TextureLibiary，存储 Ref<GPUTexture>
// 旧代码可继续通过 TextureLibiary 使用（见 Texture.h 中的兼容层）
// ---------------------------------------------------------------------------
class T_API GPUTextureLibrary {
public:
    static std::unordered_map<std::string, Ref<GPUTexture>> m_Map;
    static std::shared_mutex s_Mutex;

    static Ref<GPUTexture> Get(const std::string& path) {
        std::shared_lock lock(s_Mutex);
        auto it = m_Map.find(path);
        return (it != m_Map.end()) ? it->second : nullptr;
    }

    static void Add(const std::string& path, Ref<GPUTexture> tex) {
        std::unique_lock lock(s_Mutex);
        m_Map[path] = std::move(tex);
    }

    static void Remove(const std::string& path) {
        std::unique_lock lock(s_Mutex);
        m_Map.erase(path);
    }
};
