#include "GLTexture2D.h"
#include "Renderer.h"
#include "Debug/Debug.h"
#include "stb_image/stb_image.h"

// Helpers: RHI Format → GL enums
static unsigned int ToGLInternalFormat(Format fmt)
{
    switch (fmt)
    {
    case Format::R8_UNORM:       return GL_R8;
    case Format::RGBA8_UNORM:    return GL_RGBA8;
    case Format::RGBA8_SRGB:     return GL_SRGB8_ALPHA8;
    case Format::RG16F:          return GL_RG16F;
    case Format::RGBA16F:        return GL_RGBA16F;
    case Format::RGBA32F:        return GL_RGBA32F;
    case Format::R32_UINT:       return GL_R32UI;
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
    case Format::RG16F:          return GL_RG;
    case Format::RGBA16F:        return GL_RGBA;
    case Format::RGBA32F:        return GL_RGBA;
    case Format::R32_UINT:       return GL_RED_INTEGER;
    default:                     return GL_RGBA;
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

GLTexture2D::GLTexture2D(const Texture2DDesc& desc)
    : m_Width(desc.width), m_Height(desc.height)
    , m_Format(desc.format)
    , m_MinFilter(desc.minFilter), m_MagFilter(desc.magFilter)
    , m_WrapS(desc.wrapS), m_WrapT(desc.wrapT)
{
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
            m_Width, m_Height, 0, ToGLDataFormat(m_Format), GL_UNSIGNED_BYTE, nullptr));
        ApplySamplerParams();
        GLCall(glBindTexture(GL_TEXTURE_2D, 0));
    }
}

GLTexture2D::~GLTexture2D()
{
    if (m_RendererID)
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

void GLTexture2D::Resize(uint32_t w, uint32_t h)
{
    m_Width = w; m_Height = h;
    GLCall(glBindTexture(GL_TEXTURE_2D, m_RendererID));
    GLCall(glTexImage2D(GL_TEXTURE_2D, 0, ToGLInternalFormat(m_Format),
        m_Width, m_Height, 0, ToGLDataFormat(m_Format), GL_UNSIGNED_BYTE, nullptr));
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
}
