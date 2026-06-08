#include "GLTextureCube.h"
#include "Renderer.h"
#include "Debug/Debug.h"
#include "stb_image/stb_image.h"

GLTextureCube::GLTextureCube(const TextureCubeDesc& desc)
    : m_Size(desc.size)
{
    if (desc.facePaths.size() < 6)
    {
        Error_Core("GLTextureCube: need 6 face paths, got {}", desc.facePaths.size());
        return;
    }

    GLCall(glGenTextures(1, &m_RendererID));
    GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID));

    for (unsigned int i = 0; i < 6; i++)
    {
        int w, h, ch;
        unsigned char* data = stbi_load(desc.facePaths[i].c_str(), &w, &h, &ch, 4);
        if (data)
        {
            if (m_Size == 0) { m_Size = static_cast<uint32_t>(w); }
            GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8,
                w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data));
            stbi_image_free(data);
        }
        else
        {
            Error_Core("GLTextureCube: failed to load face {}: {}", i, desc.facePaths[i]);
        }
    }

    GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));

    GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
}

GLTextureCube::~GLTextureCube()
{
    if (m_RendererID)
        GLCall(glDeleteTextures(1, &m_RendererID));
}

void GLTextureCube::Bind(uint32_t slot)
{
    GLCall(glActiveTexture(GL_TEXTURE0 + slot));
    GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID));
}
