#pragma once

#include "Platform/RHI/RHITexture.h"

// GL implementation of RHITexture2D.
// Wraps GL_TEXTURE_2D, uses stb_image for file loading.
class T_API GLTexture2D : public RHITexture2D
{
public:
    explicit GLTexture2D(const Texture2DDesc& desc);

    // Wrap an existing GL texture (borrowed — does NOT own the texture, no glDeleteTextures).
    // Useful for bridging old GL code that creates textures directly into the RHI world.
    GLTexture2D(unsigned int existingGLID, uint32_t width, uint32_t height, Format format = Format::RGBA8_UNORM);

    ~GLTexture2D() override;

    void Bind(uint32_t slot) override;
    void Unbind() override;
    void BindAsImage(uint32_t slot, ImageAccess access) override;
    void UnbindAsImage(uint32_t slot) override;
    void Resize(uint32_t w, uint32_t h) override;
    uint32_t GetWidth() const override   { return m_Width; }
    uint32_t GetHeight() const override  { return m_Height; }
    uintptr_t GetNativeID() const override { return static_cast<uintptr_t>(m_RendererID); }
    const std::string& GetPath() const override { return m_FilePath; }

    // GL-specific (used by legacy code interop)
    unsigned int GetGLID() const { return m_RendererID; }

private:
    void CreateFromPixels(const void* data, uint32_t channels);
    void ApplySamplerParams();

    unsigned int m_RendererID = 0;
    uint32_t m_Width = 0, m_Height = 0;
    std::string m_FilePath;          // source path (empty for borrowed textures)
    Format m_Format;
    FilterMode m_MinFilter, m_MagFilter;
    WrapMode m_WrapS, m_WrapT;
    float m_BorderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };  // used when wrap == ClampToBorder
    bool m_OwnsTexture = true;       // false = wrapped (borrowed) texture
};

// Factory
inline Ref<RHITexture2D> RHITexture2D::Create(const Texture2DDesc& desc)
{
    return CreateRef<GLTexture2D>(desc);
}
