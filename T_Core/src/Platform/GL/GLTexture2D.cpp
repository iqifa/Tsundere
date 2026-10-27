#include "GLTexture2D.h"
#include "GLDebug.h"
#include "Debug/Debug.h"
#include "stb_image/stb_image.h"
#include "Platform/RenderAPIConfig.h"

#ifdef RenderAPI_OpenGL
Ref<RHITexture2D> RHITexture2D::Create(const Texture2DDesc& desc)
{
    return CreateRef<GLTexture2D>(desc);
}
#endif

// Helpers: RHI Format → GL enums
static unsigned int ToGLInternalFormat(Format fmt)
{
    switch (fmt)
    {
    case Format::R8_UNORM:       return GL_R8;
    case Format::RGBA8_UNORM:    return GL_RGBA8;
    case Format::RGBA8_SRGB:     return GL_SRGB8_ALPHA8;
    case Format::R16F:           return GL_R16F;
    case Format::RG16F:          return GL_RG16F;
    case Format::RGBA16F:        return GL_RGBA16F;
    case Format::RGBA32F:        return GL_RGBA32F;
    case Format::R32_UINT:       return GL_R32UI;
    case Format::R32_SINT:       return GL_R32I;
    case Format::D24_UNORM_S8_UINT:  return GL_DEPTH24_STENCIL8;
    case Format::D32_SFLOAT:         return GL_DEPTH_COMPONENT32F;
    case Format::D32_SFLOAT_S8_UINT: return GL_DEPTH32F_STENCIL8;
    default:                     return GL_RGBA8;
    }
}

static unsigned int ToGLDataFormat(Format fmt)
{
    switch (fmt)
    {
    case Format::R8_UNORM:       return GL_RED;
    case Format::RGBA8_UNORM:    return GL_RGBA;
    case Format::RGBA8_SRGB:     return GL_RGBA;
    case Format::R16F:           return GL_RED;
    case Format::RG16F:          return GL_RG;
    case Format::RGBA16F:        return GL_RGBA;
    case Format::RGBA32F:        return GL_RGBA;
    case Format::R32_UINT:       return GL_RED_INTEGER;
    case Format::R32_SINT:       return GL_RED_INTEGER;
    case Format::D24_UNORM_S8_UINT:  return GL_DEPTH_STENCIL;
    case Format::D32_SFLOAT:         return GL_DEPTH_COMPONENT;
    case Format::D32_SFLOAT_S8_UINT: return GL_DEPTH_STENCIL;
    default:                     return GL_RGBA;
    }
}
static unsigned int ToGlTypeFormat(Format fmt)
{
    switch (fmt)
    {
    case Format::R8_UNORM:       return GL_UNSIGNED_BYTE;
    case Format::RGBA8_UNORM:    return GL_UNSIGNED_BYTE;
    case Format::RGBA8_SRGB:     return GL_UNSIGNED_BYTE;
    case Format::R16F:           return GL_FLOAT;
    case Format::RG16F:          return GL_FLOAT;
    case Format::RGBA16F:        return GL_FLOAT;
    case Format::RGBA32F:        return GL_FLOAT;
    case Format::R32_UINT:       return GL_UNSIGNED_INT;
    case Format::R32_SINT:       return GL_INT;
    // Depth formats: type must match the internal format's component layout.
    case Format::D24_UNORM_S8_UINT:  return GL_UNSIGNED_INT_24_8;
    case Format::D32_SFLOAT:         return GL_FLOAT;
    case Format::D32_SFLOAT_S8_UINT: return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
    default:                     return GL_UNSIGNED_BYTE;
    }
}
static unsigned int ToGLWrap(WrapMode w)
{
    switch (w)
    {
    case WrapMode::Repeat:        return GL_REPEAT;
    case WrapMode::ClampToEdge:   return GL_CLAMP_TO_EDGE;
    case WrapMode::ClampToBorder: return GL_CLAMP_TO_BORDER;
    default:                      return GL_CLAMP_TO_EDGE;
    }
}

static unsigned int ToGLFilter(FilterMode f)
{
    return (f == FilterMode::Nearest) ? GL_NEAREST : GL_LINEAR;
}

GLTexture2D::GLTexture2D(unsigned int existingGLID, uint32_t width, uint32_t height, Format format)
    : m_RendererID(existingGLID), m_Width(width), m_Height(height)
    , m_Format(format)
    , m_MinFilter(FilterMode::Linear), m_MagFilter(FilterMode::Linear)
    , m_WrapS(WrapMode::ClampToEdge), m_WrapT(WrapMode::ClampToEdge)
    , m_OwnsTexture(false)   // borrowed reference — caller still owns the GL texture
{
}

GLTexture2D::GLTexture2D(const Texture2DDesc& desc)
    : m_Width(desc.width), m_Height(desc.height)
    , m_FilePath(desc.filePath)
    , m_Format(desc.format)
    , m_MinFilter(desc.minFilter), m_MagFilter(desc.magFilter)
    , m_WrapS(desc.wrapS), m_WrapT(desc.wrapT)
{
    for (int i = 0; i < 4; i++)
        m_BorderColor[i] = desc.borderColor[i];

    if (desc.pixelData)
    {
        CreateFromPixels(desc.pixelData, desc.dataChannels);
    }
    else if (!desc.filePath.empty())
    {
        stbi_set_flip_vertically_on_load(1);
        int w, h, ch;
        unsigned char* data = stbi_load(desc.filePath.c_str(), &w, &h, &ch, 0);
        if (data)
        {
            m_Width = w; m_Height = h;
            CreateFromPixels(data, ch);
            stbi_image_free(data);
        }
        else
        {
            Error_Core("GLTexture2D: failed to load {}", desc.filePath);
        }
    }
    else
    {
        // Empty texture (for FBO attachments etc.)
        GLCall(glGenTextures(1, &m_RendererID));
        GLCall(glBindTexture(GL_TEXTURE_2D, m_RendererID));
        GLCall(glTexImage2D(GL_TEXTURE_2D, 0, ToGLInternalFormat(m_Format),
            m_Width, m_Height, 0, ToGLDataFormat(m_Format), ToGlTypeFormat(m_Format), nullptr));
        ApplySamplerParams();
        GLCall(glBindTexture(GL_TEXTURE_2D, 0));
    }
}

GLTexture2D::~GLTexture2D()
{
    if (m_OwnsTexture && m_RendererID)
        GLCall(glDeleteTextures(1, &m_RendererID));
}

void GLTexture2D::Bind(uint32_t slot)
{
    GLCall(glActiveTexture(GL_TEXTURE0 + slot));
    GLCall(glBindTexture(GL_TEXTURE_2D, m_RendererID));
}

void GLTexture2D::Unbind()
{
    GLCall(glBindTexture(GL_TEXTURE_2D, 0));
}

static unsigned int ToGLAccess(ImageAccess access)
{
    switch (access)
    {
    case ImageAccess::ReadOnly:  return GL_READ_ONLY;
    case ImageAccess::WriteOnly: return GL_WRITE_ONLY;
    case ImageAccess::ReadWrite: return GL_READ_WRITE;
    default:                     return GL_READ_WRITE;
    }
}

void GLTexture2D::BindAsImage(uint32_t slot, ImageAccess access)
{
    GLCall(glBindImageTexture(slot, m_RendererID, 0, GL_FALSE, 0,
        ToGLAccess(access), ToGLInternalFormat(m_Format)));
}

void GLTexture2D::UnbindAsImage(uint32_t slot)
{
    GLCall(glBindImageTexture(slot, 0, 0, GL_FALSE, 0, GL_READ_ONLY, ToGLInternalFormat(m_Format)));
}

void GLTexture2D::Resize(uint32_t w, uint32_t h)
{
    m_Width = w; m_Height = h;
    GLCall(glBindTexture(GL_TEXTURE_2D, m_RendererID));
    GLCall(glTexImage2D(GL_TEXTURE_2D, 0, ToGLInternalFormat(m_Format),
        m_Width, m_Height, 0, ToGLDataFormat(m_Format), ToGlTypeFormat(m_Format), nullptr));
    GLCall(glBindTexture(GL_TEXTURE_2D, 0));
}

void GLTexture2D::CreateFromPixels(const void* data, uint32_t channels)
{
    GLCall(glGenTextures(1, &m_RendererID));
    GLCall(glBindTexture(GL_TEXTURE_2D, m_RendererID));

    // Map channel count to GL format for the *upload* (not internal storage)
    unsigned int uploadFmt = GL_RGBA;
    if (channels == 1)      uploadFmt = GL_RED;
    else if (channels == 3) uploadFmt = GL_RGB;
    else if (channels == 4) uploadFmt = GL_RGBA;

    GLCall(glTexImage2D(GL_TEXTURE_2D, 0, ToGLInternalFormat(m_Format),
        m_Width, m_Height, 0, uploadFmt, GL_UNSIGNED_BYTE, data));

    ApplySamplerParams();
    GLCall(glBindTexture(GL_TEXTURE_2D, 0));
}

void GLTexture2D::ApplySamplerParams()
{
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ToGLFilter(m_MinFilter)));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ToGLFilter(m_MagFilter)));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGLWrap(m_WrapS)));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGLWrap(m_WrapT)));

    // Border color only matters for CLAMP_TO_BORDER. Shadow maps rely on a white
    // border so out-of-bounds lookups sample "fully lit" instead of "in shadow".
    if (m_WrapS == WrapMode::ClampToBorder || m_WrapT == WrapMode::ClampToBorder)
        GLCall(glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, m_BorderColor));
}
