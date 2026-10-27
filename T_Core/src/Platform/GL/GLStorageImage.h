#pragma once

#include "Platform/RHI/RHITexture.h"
#include"Platform/RenderAPIConfig.h"
// GL implementation of RHIStorageImage.
// Wraps a GL_TEXTURE_2D suitable for glBindImageTexture (compute shader).
class T_API GLStorageImage : public RHIStorageImage
{
public:
    explicit GLStorageImage(const StorageImageDesc& desc);
    ~GLStorageImage() override;

    void BindAsImage(uint32_t slot, ImageAccess access) override;
    void UnbindAsImage(uint32_t slot) override;
    void BindAsTexture(uint32_t slot) override;
    void Resize(uint32_t w, uint32_t h) override;

    uint32_t GetWidth() const override  { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    uintptr_t GetNativeID() const override { return static_cast<uintptr_t>(m_RendererID); }

    unsigned int GetGLID() const { return m_RendererID; }

private:
    void CreateTexture();

    unsigned int m_RendererID = 0;
    uint32_t m_Width = 0, m_Height = 0;
    unsigned int m_GLInternalFmt = 0;
    unsigned int m_GLFormat = 0;
    unsigned int m_GLType = 0;
};
