#pragma once

#include "Platform/RHI/RHIFramebuffer.h"
#include "Platform/RHI/RHITexture.h"

// GL implementation of RHIFramebuffer.
// Wraps an OpenGL FBO with color + depth attachments.
// Supports MSAA (samples > 1) via GL_TEXTURE_2D_MULTISAMPLE.
class T_API GLFramebuffer : public RHIFramebuffer
{
public:
    explicit GLFramebuffer(const FramebufferDesc& desc);
    ~GLFramebuffer() override;

    void Bind() override;
    void Unbind() override;
    void Resize(uint32_t w, uint32_t h) override;

    uint32_t GetWidth() const override  { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }

    uintptr_t GetColorAttachmentID(uint32_t index = 0) const override;
    uintptr_t GetDepthAttachmentID() const override;
    uintptr_t GetFramebufferID() const override;
    uint32_t GetColorAttachmentCount() const override { return m_ColorAttachmentCount; }
    Format GetColorAttachmentFormat(uint32_t index = 0) const override
    {
        return index < m_ColorFormats.size() ? m_ColorFormats[index] : Format::Unknown;
    }
    Format GetDepthAttachmentFormat() const override
    {
        return m_HasDepthStencil ? Format::D24_UNORM_S8_UINT : Format::Unknown;
    }
    uint32_t GetSampleCount() const override { return m_Samples; }

    RHITexture2D* GetColorAttachment(uint32_t index = 0) const override;
    RHITexture2D* GetDepthAttachment() const override;

    // MSAA resolve
    void ResolveTo(Ref<RHIFramebuffer> dst) override;

    // GL-specific (legacy interop)
    unsigned int GetGLFboID() const    { return m_FboID; }
    bool IsMSAA() const                { return m_Samples > 1; }

private:
    void Invalidate();           // (re)create all GL resources
    void Cleanup();              // delete all GL resources
    unsigned int GLCreateAttachment(Format fmt, uint32_t samples);

    unsigned int m_FboID = 0;
    std::vector<unsigned int> m_ColorAttachments;       // GL texture IDs
    unsigned int m_DepthStencilAttachment = 0;
    uint32_t m_Width = 0, m_Height = 0;
    uint32_t m_Samples = 1;
    uint32_t m_ColorAttachmentCount = 1;
    std::vector<Format> m_ColorFormats;
    bool m_HasDepthStencil = true;

    // Borrowed RHITexture2D wrappers over the raw GL attachment textures, so
    // GetColorAttachment()/GetDepthAttachment() can return an RHI object
    // without duplicating the texture. Do not delete — owned by the raw IDs.
    std::vector<Ref<RHITexture2D>> m_ColorAttachmentRefs;
    Ref<RHITexture2D> m_DepthAttachmentRef;
};

// Non-owning framebuffer over externally-owned textures.
//
// Unlike GLFramebuffer, this does NOT create or delete attachment textures.
// It only owns the FBO name. Used by RenderGraph, which allocates its own
// textures and needs somewhere to bind them as render targets.
class T_API GLFramebufferView : public RHIFramebuffer
{
public:
    explicit GLFramebufferView(const FramebufferViewDesc& desc);
    ~GLFramebufferView() override;

    void Bind() override;
    void Unbind() override;

    // A view has no say over its attachments' size — the owner resizes them.
    void Resize(uint32_t w, uint32_t h) override {}

    uint32_t GetWidth() const override  { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }

    uintptr_t GetColorAttachmentID(uint32_t index = 0) const override;
    uintptr_t GetDepthAttachmentID() const override;
    uintptr_t GetFramebufferID() const override { return static_cast<uintptr_t>(m_FboID); }
    uint32_t GetColorAttachmentCount() const override
    {
        return static_cast<uint32_t>(m_ColorAttachments.size());
    }

    Format GetColorAttachmentFormat(uint32_t index = 0) const override
    {
        return index < m_ColorAttachmentRefs.size()
            ? m_ColorAttachmentRefs[index]->GetFormat()
            : Format::Unknown;
    }
    Format GetDepthAttachmentFormat() const override
    {
        return m_DepthAttachmentRef ? m_DepthAttachmentRef->GetFormat() : Format::Unknown;
    }
    uint32_t GetSampleCount() const override { return 1; }

    RHITexture2D* GetColorAttachment(uint32_t index = 0) const override;
    RHITexture2D* GetDepthAttachment() const override;

private:
    unsigned int m_FboID = 0;

    // Borrowed — owned by the caller (RenderGraph), never deleted here.
    std::vector<unsigned int> m_ColorAttachments;   // GL texture IDs
    unsigned int m_DepthAttachment = 0;

    // Non-owning RHI pointers to the same attachments, for GetColorAttachment().
    std::vector<RHITexture2D*> m_ColorAttachmentRefs;
    RHITexture2D* m_DepthAttachmentRef = nullptr;

    uint32_t m_Width = 0, m_Height = 0;
};
