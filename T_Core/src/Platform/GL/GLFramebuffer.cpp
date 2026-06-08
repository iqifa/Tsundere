#include "GLFramebuffer.h"
#include "Renderer.h"
#include "Debug/Debug.h"

// Helpers: RHI Format → GL enums
static unsigned int ToGLInternalFormat(Format fmt)
{
    switch (fmt)
    {
    case Format::R8_UNORM:              return GL_R8;
    case Format::RGBA8_UNORM:           return GL_RGBA8;
    case Format::RGBA8_SRGB:            return GL_SRGB8_ALPHA8;
    case Format::RG16F:                 return GL_RG16F;
    case Format::RGBA16F:               return GL_RGBA16F;
    case Format::RGBA32F:               return GL_RGBA32F;
    case Format::D24_UNORM_S8_UINT:     return GL_DEPTH24_STENCIL8;
    case Format::D32_SFLOAT:            return GL_DEPTH_COMPONENT32F;
    default:                            return GL_RGBA8;
    }
}

static unsigned int ToGLDataFormat(Format fmt)
{
    switch (fmt)
    {
    case Format::R8_UNORM:              return GL_RED;
    case Format::RGBA8_UNORM:           return GL_RGBA;
    case Format::RGBA8_SRGB:            return GL_RGBA;
    case Format::RG16F:                 return GL_RG;
    case Format::RGBA16F:               return GL_RGBA;
    case Format::RGBA32F:               return GL_RGBA;
    case Format::D24_UNORM_S8_UINT:     return GL_DEPTH_STENCIL;
    case Format::D32_SFLOAT:            return GL_DEPTH_COMPONENT;
    default:                            return GL_RGBA;
    }
}

GLFramebuffer::GLFramebuffer(const FramebufferDesc& desc)
    : m_Width(desc.width), m_Height(desc.height)
    , m_Samples(desc.samples)
    , m_HasDepthStencil(desc.hasDepthStencil)
{
    // Clamp MSAA samples to hardware max
    if (m_Samples > 1)
    {
        GLint maxSamples = 1;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        if (static_cast<GLint>(m_Samples) > maxSamples)
        {
            Warn_Core("GLFramebuffer: MSAA samples clamped from {} to {}", m_Samples, maxSamples);
            m_Samples = static_cast<uint32_t>(maxSamples);
        }
    }

    // Copy color attachment formats
    if (desc.colorAttachments.empty())
    {
        m_ColorFormats.push_back(Format::RGBA8_UNORM);
    }
    else
    {
        for (auto& att : desc.colorAttachments)
            m_ColorFormats.push_back(att.format);
    }
    m_ColorAttachmentCount = static_cast<uint32_t>(m_ColorFormats.size());

    Invalidate();
}

GLFramebuffer::~GLFramebuffer()
{
    Cleanup();
}

void GLFramebuffer::Bind()
{
    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, m_FboID));
    GLCall(glViewport(0, 0, m_Width, m_Height));
}

void GLFramebuffer::Unbind()
{
    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void GLFramebuffer::Resize(uint32_t w, uint32_t h)
{
    if (w == m_Width && h == m_Height) return;
    m_Width = w; m_Height = h;
    Invalidate();
}

uintptr_t GLFramebuffer::GetColorAttachmentID(uint32_t index) const
{
    if (index < m_ColorAttachments.size())
        return static_cast<uintptr_t>(m_ColorAttachments[index]);
    return 0;
}

uintptr_t GLFramebuffer::GetDepthAttachmentID() const
{
    return static_cast<uintptr_t>(m_DepthStencilAttachment);
}

uintptr_t GLFramebuffer::GetFramebufferID() const
{
    return static_cast<uintptr_t>(m_FboID);
}

void GLFramebuffer::ResolveTo(Ref<RHIFramebuffer> dst)
{
    if (m_Samples <= 1 || !dst) return;

    // Resolve each color attachment
    for (uint32_t i = 0; i < m_ColorAttachmentCount && i < dst->GetColorAttachmentCount(); i++)
    {
        GLCall(glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FboID));
        GLCall(glReadBuffer(GL_COLOR_ATTACHMENT0 + i));
        GLCall(glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
            static_cast<unsigned int>(dst->GetFramebufferID())));
        GLCall(glDrawBuffer(GL_COLOR_ATTACHMENT0 + i));
        GLCall(glBlitFramebuffer(0, 0, m_Width, m_Height,
            0, 0, dst->GetWidth(), dst->GetHeight(),
            GL_COLOR_BUFFER_BIT, GL_NEAREST));
    }

    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void GLFramebuffer::Cleanup()
{
    if (m_FboID)
    {
        GLCall(glDeleteFramebuffers(1, &m_FboID));
        m_FboID = 0;
    }
    for (auto& tex : m_ColorAttachments)
    {
        if (tex)
            GLCall(glDeleteTextures(1, &tex));
    }
    m_ColorAttachments.clear();
    if (m_DepthStencilAttachment)
    {
        GLCall(glDeleteTextures(1, &m_DepthStencilAttachment));
        m_DepthStencilAttachment = 0;
    }
}

void GLFramebuffer::Invalidate()
{
    Cleanup();

    GLCall(glGenFramebuffers(1, &m_FboID));
    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, m_FboID));

    unsigned int target = (m_Samples > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

    // Color attachments
    m_ColorAttachments.resize(m_ColorAttachmentCount, 0);
    for (uint32_t i = 0; i < m_ColorAttachmentCount; i++)
    {
        unsigned int texID = GLCreateAttachment(m_ColorFormats[i], m_Samples);
        m_ColorAttachments[i] = texID;

        GLCall(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
            target, texID, 0));
    }

    // Depth-stencil attachment
    if (m_HasDepthStencil)
    {
        Format depthFmt = Format::D24_UNORM_S8_UINT;
        m_DepthStencilAttachment = GLCreateAttachment(depthFmt, m_Samples);

        GLCall(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
            target, m_DepthStencilAttachment, 0));
    }

    // Check completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        Error_Core("GLFramebuffer: framebuffer is not complete!");

    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

unsigned int GLFramebuffer::GLCreateAttachment(Format fmt, uint32_t samples)
{
    unsigned int texID = 0;
    GLCall(glGenTextures(1, &texID));

    unsigned int target = (samples > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    GLCall(glBindTexture(target, texID));

    if (samples > 1)
    {
        GLCall(glTexImage2DMultisample(target, samples, ToGLInternalFormat(fmt),
            m_Width, m_Height, GL_TRUE));
    }
    else
    {
        GLCall(glTexImage2D(target, 0, ToGLInternalFormat(fmt),
            m_Width, m_Height, 0, ToGLDataFormat(fmt), GL_UNSIGNED_BYTE, nullptr));
        GLCall(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GLCall(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GLCall(glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GLCall(glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    }

    GLCall(glBindTexture(target, 0));
    return texID;
}
