#pragma once

#include "Platform/RHI/RHITexture.h"

// GL implementation of RHITextureCube.
// Wraps GL_TEXTURE_CUBE_MAP, uses stb_image for 6-face loading.
class T_API GLTextureCube : public RHITextureCube
{
public:
    explicit GLTextureCube(const TextureCubeDesc& desc);
    ~GLTextureCube() override;

    void Bind(uint32_t slot) override;
    uintptr_t GetNativeID() const override { return static_cast<uintptr_t>(m_RendererID); }

    unsigned int GetGLID() const { return m_RendererID; }

private:
    unsigned int m_RendererID = 0;
    uint32_t m_Size = 0;
};

// Factory
inline Ref<RHITextureCube> RHITextureCube::Create(const TextureCubeDesc& desc)
{
    return CreateRef<GLTextureCube>(desc);
}
