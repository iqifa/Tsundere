#include "GLStorageImage.h"
#include "GLDebug.h"

static void GetGLFormat(Format fmt, unsigned int& internalFmt, unsigned int& dataFmt, unsigned int& dataType)
{
    switch (fmt)
    {
    case Format::RGBA32F:
        internalFmt = GL_RGBA32F; dataFmt = GL_RGBA; dataType = GL_FLOAT; break;
    case Format::RGBA16F:
        internalFmt = GL_RGBA16F; dataFmt = GL_RGBA; dataType = GL_FLOAT; break;
    case Format::RG16F:
        internalFmt = GL_RG16F;   dataFmt = GL_RG;   dataType = GL_FLOAT; break;
    case Format::R16F:
        internalFmt = GL_R16F;    dataFmt = GL_RED;  dataType = GL_FLOAT; break;
    case Format::R8_UNORM:
        internalFmt = GL_R8;      dataFmt = GL_RED;  dataType = GL_UNSIGNED_BYTE; break;
    case Format::R32_UINT:
        internalFmt = GL_R32UI;   dataFmt = GL_RED_INTEGER; dataType = GL_UNSIGNED_INT; break;
    default:  // fallback
        internalFmt = GL_RGBA32F; dataFmt = GL_RGBA; dataType = GL_FLOAT; break;
    }
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

GLStorageImage::GLStorageImage(const StorageImageDesc& desc)
    : m_Width(desc.width), m_Height(desc.height)
{
    GetGLFormat(desc.format, m_GLInternalFmt, m_GLFormat, m_GLType);
    CreateTexture();
}

GLStorageImage::~GLStorageImage()
{
    if (m_RendererID)
        GLCall(glDeleteTextures(1, &m_RendererID));
}

void GLStorageImage::CreateTexture()
{
    if (m_RendererID)
        GLCall(glDeleteTextures(1, &m_RendererID));

    GLCall(glGenTextures(1, &m_RendererID));
    GLCall(glBindTexture(GL_TEXTURE_2D, m_RendererID));
    GLCall(glTexImage2D(GL_TEXTURE_2D, 0, m_GLInternalFmt, m_Width, m_Height,
        0, m_GLFormat, m_GLType, nullptr));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GLCall(glBindTexture(GL_TEXTURE_2D, 0));
}

void GLStorageImage::BindAsImage(uint32_t slot, ImageAccess access)
{
    GLCall(glBindImageTexture(slot, m_RendererID, 0, GL_FALSE, 0,
        ToGLAccess(access), m_GLInternalFmt));
}

void GLStorageImage::UnbindAsImage(uint32_t slot)
{
    // Read-only is arbitrary here — unbinding just detaches the image unit.
    GLCall(glBindImageTexture(slot, 0, 0, GL_FALSE, 0, GL_READ_ONLY, m_GLInternalFmt));
}

void GLStorageImage::BindAsTexture(uint32_t slot)
{
    GLCall(glActiveTexture(GL_TEXTURE0 + slot));
    GLCall(glBindTexture(GL_TEXTURE_2D, m_RendererID));
}

void GLStorageImage::Resize(uint32_t w, uint32_t h)
{
    m_Width = w; m_Height = h;
    CreateTexture();
}
#ifdef RenderAPI_OpenGL
Ref<RHIStorageImage> RHIStorageImage::Create(const StorageImageDesc& desc)
{
    return CreateRef<GLStorageImage>(desc);
}
#endif // RenderAPI_OpenGL