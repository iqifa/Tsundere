#include "GLBuffer.h"
#include "GLDebug.h"

GLBuffer::GLBuffer(const BufferDesc& desc)
    : m_Size(desc.size)
    , m_Usage(desc.usage)
    , m_CpuAccess(desc.cpuAccess)
{
    GLCall(glGenBuffers(1, &m_RendererID));
    GLCall(glBindBuffer(GetGLTarget(), m_RendererID));

    GLenum glUsage = m_CpuAccess ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    GLCall(glBufferData(GetGLTarget(), m_Size, desc.initialData, glUsage));

    if (m_Usage == BufferUsage::Storage && desc.initialData)
    {
        // For SSBOs, bind to base slot 0 by default (caller can rebind)
        GLCall(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_RendererID));
    }

    GLCall(glBindBuffer(GetGLTarget(), 0));
}

GLBuffer::~GLBuffer()
{
    if (m_RendererID)
    {
        GLCall(glDeleteBuffers(1, &m_RendererID));
    }
}

void GLBuffer::Upload(const void* data, uint32_t size, uint32_t offset)
{
    GLCall(glBindBuffer(GetGLTarget(), m_RendererID));
    GLCall(glBufferSubData(GetGLTarget(), offset, size, data));
    if (m_Usage == BufferUsage::Storage)
    {
        GLCall(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_RendererID));
    }
    GLCall(glBindBuffer(GetGLTarget(), 0));
}

void* GLBuffer::Map()
{
    GLCall(glBindBuffer(GetGLTarget(), m_RendererID));
    void* ptr = glMapBuffer(GetGLTarget(), GL_READ_WRITE);
    GLCall(glBindBuffer(GetGLTarget(), 0));
    return ptr;
}

void GLBuffer::Unmap()
{
    GLCall(glBindBuffer(GetGLTarget(), m_RendererID));
    GLCall(glUnmapBuffer(GetGLTarget()));
    GLCall(glBindBuffer(GetGLTarget(), 0));
}

void GLBuffer::Bind() const
{
    GLCall(glBindBuffer(GetGLTarget(), m_RendererID));
}

void GLBuffer::Unbind() const
{
    GLCall(glBindBuffer(GetGLTarget(), 0));
}

void GLBuffer::BindToSlot(uint32_t slot) const
{
    if (m_Usage == BufferUsage::Storage)
    {
        GLCall(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, m_RendererID));
    }
    else if (m_Usage == BufferUsage::Uniform)
    {
        GLCall(glBindBufferBase(GL_UNIFORM_BUFFER, slot, m_RendererID));
    }
}

unsigned int GLBuffer::GetGLTarget() const
{
    switch (m_Usage)
    {
    case BufferUsage::Vertex:   return GL_ARRAY_BUFFER;
    case BufferUsage::Index:    return GL_ELEMENT_ARRAY_BUFFER;
    case BufferUsage::Uniform:  return GL_UNIFORM_BUFFER;
    case BufferUsage::Storage:  return GL_SHADER_STORAGE_BUFFER;
    case BufferUsage::Staging:  return GL_COPY_READ_BUFFER;
    default:                    return GL_ARRAY_BUFFER;
    }
}
