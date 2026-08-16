#include "GLFramebuffer.h"
#include "Renderer.h"
#include "Debug/Debug.h"
#include "Platform/RHI/RHITexture.h"   // GLFramebufferView needs RHITexture2D::GetNativeID()

// A combined depth-stencil format must bind to GL_DEPTH_STENCIL_ATTACHMENT;
// binding it to GL_DEPTH_ATTACHMENT leaves the FBO incomplete.
static bool IsDepthStencilFormat(Format fmt)
{
    return fmt == Format::D24_UNORM_S8_UINT
        || fmt == Format::D32_SFLOAT_S8_UINT;
}

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
    case Format::D32_SFLOAT_S8_UINT:    return GL_DEPTH32F_STENCIL8;
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
    case Format::D32_SFLOAT_S8_UINT:    return GL_DEPTH_STENCIL;
    default:                            return GL_RGBA;
    }
}
static unsigned int ToGlTypeFormat(Format fmt)
{
    switch (fmt)
    {
    case Format::R8_UNORM:              return GL_UNSIGNED_BYTE;
    case Format::RGBA8_UNORM:           return GL_UNSIGNED_BYTE;
    case Format::RGBA8_SRGB:            return GL_UNSIGNED_BYTE;
    case Format::RG16F:                 return GL_FLOAT;
    case Format::RGBA16F:               return GL_FLOAT;
    case Format::RGBA32F:               return GL_FLOAT;
    case Format::D24_UNORM_S8_UINT:     return GL_UNSIGNED_INT_24_8;
    case Format::D32_SFLOAT:            return GL_FLOAT;
    case Format::D32_SFLOAT_S8_UINT:    return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
    default:                     return GL_UNSIGNED_BYTE;
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

    unsigned int dstFbo = static_cast<unsigned int>(dst->GetFramebufferID());
    uint32_t dstCount = dst->GetColorAttachmentCount();
    uint32_t resolveCount = m_ColorAttachmentCount < dstCount ? m_ColorAttachmentCount : dstCount;

    // Resolve each color attachment individually (glBlitFramebuffer only
    // copies the single read buffer → single draw buffer at a time).
    for (uint32_t i = 0; i < resolveCount; i++)
    {
        GLCall(glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FboID));
        GLCall(glReadBuffer(GL_COLOR_ATTACHMENT0 + i));
        GLCall(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo));
        GLCall(glDrawBuffer(GL_COLOR_ATTACHMENT0 + i));
        GLCall(glBlitFramebuffer(0, 0, m_Width, m_Height,
            0, 0, dst->GetWidth(), dst->GetHeight(),
            GL_COLOR_BUFFER_BIT, GL_NEAREST));
    }

    // Restore MRT draw buffers on destination FBO.
    // glDrawBuffer() (singular) overrides the glDrawBuffers() (plural, MRT)
    // set by Invalidate(), which would leave only the last attachment enabled
    // for subsequent rendering — causing a black screen after MSAA is toggled off.
    GLCall(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo));
    std::vector<GLenum> drawBufs(dstCount);
    for (uint32_t i = 0; i < dstCount; i++)
        drawBufs[i] = GL_COLOR_ATTACHMENT0 + i;
    GLCall(glDrawBuffers(dstCount, drawBufs.data()));

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

    // Enable all color attachments for MRT (glDrawBuffers defaults to only ATTACHMENT0)
    {
        std::vector<GLenum> drawBufs(m_ColorAttachmentCount);
        for (uint32_t i = 0; i < m_ColorAttachmentCount; i++)
            drawBufs[i] = GL_COLOR_ATTACHMENT0 + i;
        GLCall(glDrawBuffers(m_ColorAttachmentCount, drawBufs.data()));
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
            m_Width, m_Height, 0, ToGLDataFormat(fmt), ToGlTypeFormat(fmt), nullptr));
        GLCall(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GLCall(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GLCall(glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GLCall(glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    }

    GLCall(glBindTexture(target, 0));
    return texID;
}

// =============================================================================
// GLFramebufferView — non-owning FBO over externally-owned textures
// =============================================================================

GLFramebufferView::GLFramebufferView(const FramebufferViewDesc& desc)
    : m_Width(desc.width), m_Height(desc.height)
{
    GLCall(glGenFramebuffers(1, &m_FboID));
    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, m_FboID));

    std::vector<GLenum> drawBufs;
    drawBufs.reserve(desc.colorAttachments.size());

    for (size_t i = 0; i < desc.colorAttachments.size(); i++)
    {
        RHITexture2D* tex = desc.colorAttachments[i];
        if (!tex)
        {
            Error_Core("GLFramebufferView: color attachment {} is null", i);
            continue;
        }

        unsigned int texID = static_cast<unsigned int>(tex->GetNativeID());
        m_ColorAttachments.push_back(texID);

        GLCall(glFramebufferTexture2D(GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
            GL_TEXTURE_2D, texID, 0));

        drawBufs.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
    }

    // glDrawBuffers defaults to COLOR_ATTACHMENT0 only, so MRT needs it explicitly.
    // A depth-only FBO must instead declare that it draws/reads no color at all,
    // otherwise it is incomplete.
    if (!drawBufs.empty())
    {
        GLCall(glDrawBuffers(static_cast<GLsizei>(drawBufs.size()), drawBufs.data()));
    }
    else
    {
        GLCall(glDrawBuffer(GL_NONE));
        GLCall(glReadBuffer(GL_NONE));
    }

    if (desc.depthAttachment)
    {
        m_DepthAttachment = static_cast<unsigned int>(desc.depthAttachment->GetNativeID());

        GLenum attachPoint = IsDepthStencilFormat(desc.depthFormat)
            ? GL_DEPTH_STENCIL_ATTACHMENT
            : GL_DEPTH_ATTACHMENT;

        GLCall(glFramebufferTexture2D(GL_FRAMEBUFFER, attachPoint,
            GL_TEXTURE_2D, m_DepthAttachment, 0));
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Error_Core("GLFramebufferView: framebuffer incomplete (status 0x{:X}, "
                   "{} color, depth={})",
            static_cast<unsigned int>(status),
            m_ColorAttachments.size(),
            m_DepthAttachment);
    }

    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

GLFramebufferView::~GLFramebufferView()
{
    // Only the FBO name is ours. Attachment textures belong to the caller.
    if (m_FboID)
        GLCall(glDeleteFramebuffers(1, &m_FboID));
}

void GLFramebufferView::Bind()
{
    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, m_FboID));
    GLCall(glViewport(0, 0, m_Width, m_Height));
}

void GLFramebufferView::Unbind()
{
    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

uintptr_t GLFramebufferView::GetColorAttachmentID(uint32_t index) const
{
    if (index < m_ColorAttachments.size())
        return static_cast<uintptr_t>(m_ColorAttachments[index]);
    return 0;
}

uintptr_t GLFramebufferView::GetDepthAttachmentID() const
{
    return static_cast<uintptr_t>(m_DepthAttachment);
}
