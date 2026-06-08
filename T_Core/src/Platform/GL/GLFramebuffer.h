#pragma once

#include "Platform/RHI/RHIFramebuffer.h"
#include "FrameBuffer.h"   // FrameBufferSpecification for legacy interop

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
};

// Factory
inline Ref<RHIFramebuffer> RHIFramebuffer::Create(const FramebufferDesc& desc)
{
    return CreateRef<GLFramebuffer>(desc);
}
