#include "GLDescriptorSet.h"
#include "GLBuffer.h"
#include "GLDebug.h"
#include <GL/glew.h>

void GLDescriptorSet::BindTexture(uint32_t binding, Ref<RHITexture2D> texture, uint32_t unit)
{
    (void)binding; // GL uses implicit unit mapping
    if (texture)
        m_Textures.push_back({ texture, unit });
}

void GLDescriptorSet::BindCubeMap(uint32_t binding, Ref<RHITextureCube> cubemap, uint32_t unit)
{
    (void)binding;
    if (cubemap)
        m_Cubemaps.push_back({ cubemap, unit });
}

void GLDescriptorSet::BindStorageImage(uint32_t binding, Ref<RHIStorageImage> image, ImageAccess access, uint32_t unit)
{
    (void)binding;
    if (image)
        m_StorageImages.push_back({ image, unit, access });
}

void GLDescriptorSet::BindUniformBuffer(uint32_t binding, Ref<RHIBuffer> buffer)
{
    if (buffer)
        m_Buffers.push_back({ buffer, binding, false });
}

void GLDescriptorSet::BindStorageBuffer(uint32_t binding, Ref<RHIBuffer> buffer)
{
    if (buffer)
        m_Buffers.push_back({ buffer, binding, true });
}

void GLDescriptorSet::Apply(uint32_t slot)
{
    (void)slot;

    // Bind textures to GL texture units
    for (auto& tb : m_Textures)
    {
        if (tb.texture)
            tb.texture->Bind(tb.unit);
    }

    // Bind cube maps
    for (auto& cb : m_Cubemaps)
    {
        if (cb.cubemap)
            cb.cubemap->Bind(cb.unit);
    }

    // Bind storage images
    for (auto& si : m_StorageImages)
    {
        if (si.image)
            si.image->BindAsImage(si.unit, si.access);
    }

    // Bind buffers (uniform / storage)
    for (auto& bb : m_Buffers)
    {
        if (bb.buffer)
        {
            auto* glBuf = static_cast<GLBuffer*>(bb.buffer.get());
            if (bb.isStorage)
            {
                GLCall(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bb.slot, glBuf->GetGLID()));
            }
            else
            {
                GLCall(glBindBufferBase(GL_UNIFORM_BUFFER, bb.slot, glBuf->GetGLID()));
            }
        }
    }
}

void GLDescriptorSet::Reset()
{
    m_Textures.clear();
    m_Cubemaps.clear();
    m_StorageImages.clear();
    m_Buffers.clear();
}
