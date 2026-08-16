#include "GPUTexture.h"
#include "stb_image/stb_image.h"   // stbi_image_free (used by CreateFromPixels)

// Static member definitions
std::unordered_map<std::string, Ref<GPUTexture>> GPUTextureLibrary::m_Map;
std::shared_mutex GPUTextureLibrary::s_Mutex;

// ── 析构：把 glDeleteTextures 推入延迟队列，确保在 GL 主线程执行 ──
GPUTexture::~GPUTexture()
{
    unsigned id = m_ID;
    if (id != 0) {
        GPUDeletionQueue::Enqueue([id]() {
            glDeleteTextures(1, &id);
        });
        m_ID = 0;
    }
}

GPUTexture::GPUTexture(GPUTexture&& o) noexcept
    : m_ID(o.m_ID), m_Width(o.m_Width), m_Height(o.m_Height),
      m_Channels(o.m_Channels), m_Path(std::move(o.m_Path))
{
    o.m_ID = 0;  // 转移所有权，防止双重 delete
}

GPUTexture& GPUTexture::operator=(GPUTexture&& o) noexcept
{
    if (this != &o) {
        // 先释放当前 GL 资源
        unsigned id = m_ID;
        if (id != 0) {
            GPUDeletionQueue::Enqueue([id]() { glDeleteTextures(1, &id); });
        }
        m_ID       = o.m_ID;
        m_Width    = o.m_Width;
        m_Height   = o.m_Height;
        m_Channels = o.m_Channels;
        m_Path     = std::move(o.m_Path);
        o.m_ID = 0;
    }
    return *this;
}

void GPUTexture::Bind(unsigned int slot) const
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_ID);
}

void GPUTexture::UnBind() const
{
    glBindTexture(GL_TEXTURE_2D, 0);
}

size_t GPUTexture::GPUSize() const
{
    // RGBA8 估算，忽略 mip（暂未启用）
    return static_cast<size_t>(m_Width) * m_Height * 4;
}

void GPUTexture::Upload(const unsigned char* pixels, int w, int h, int channels)
{
    GLenum format = GL_RGBA;
    if      (channels == 1) format = GL_RED;
    else if (channels == 3) format = GL_RGB;
    else if (channels == 4) format = GL_RGBA;

    glGenTextures(1, &m_ID);
    glBindTexture(GL_TEXTURE_2D, m_ID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h,
                 0, format, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_Width    = w;
    m_Height   = h;
    m_Channels = channels;
}

Ref<GPUTexture> GPUTexture::CreateFromAsset(const TextureAsset& asset)
{
    if (!asset.IsValid()) return nullptr;

    auto tex = CreateRef<GPUTexture>();
    tex->m_Path = asset.path;
    tex->Upload(asset.pixels.data(), asset.width, asset.height, asset.channels);
    return tex;
}

Ref<GPUTexture> GPUTexture::CreateFromPixels(const std::string& path,
    unsigned char* pixelData, int width, int height, int channels)
{
    if (!pixelData) return nullptr;

    auto tex = CreateRef<GPUTexture>();
    tex->m_Path = path;
    tex->Upload(pixelData, width, height, channels);

    // 释放 stbi 数据（与旧 Texture::CreateFromPixels 行为一致）
    stbi_image_free(pixelData);
    return tex;
}
