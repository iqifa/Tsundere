#include "GLDescriptorSet.h"
#include "GLBuffer.h"
#include "GLDebug.h"
#include <GL/glew.h>

void GLDescriptorSet::BindTexture(uint32_t binding, Ref<RHITexture2D> texture, uint32_t unit)
{
    (void)binding; // GL uses implicit unit mapping
    if (texture)
        m_Textures.push_back({ texture, texture.get(), unit });
}

void GLDescriptorSet::BindTexture(uint32_t binding, RHITexture2D* texture, uint32_t unit)
{
    (void)binding;
    if (texture)
        m_Textures.push_back({ nullptr, texture, unit });
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

void GLDescriptorSet::BindUniformBuffer(uint32_t binding, Ref<RHIBuffer> buffer, uint32_t dynamicRange)
{
    if (buffer)
        m_Buffers.push_back({ buffer, binding, false, dynamicRange });
}

void GLDescriptorSet::BindStorageBuffer(uint32_t binding, Ref<RHIBuffer> buffer)
{
    if (buffer)
        m_Buffers.push_back({ buffer, binding, true, 0 });
}

void GLDescriptorSet::MarkBindingAsDynamic(uint32_t binding)
{
    (void)binding;
    // OpenGL doesn't need explicit dynamic marking - glBindBufferRange
    // is used automatically in Apply() when dynamic offsets are provided
}

void GLDescriptorSet::Apply(uint32_t slot, const uint32_t* dynamicOffsets, uint32_t dynamicOffsetCount)
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
    uint32_t dynamicOffsetIndex = 0;
    for (auto& bb : m_Buffers)
    {
        if (bb.buffer)
        {
            auto* glBuf = static_cast<GLBuffer*>(bb.buffer.get());

            // If dynamic offsets are provided, use glBindBufferRange; otherwise use glBindBufferBase
            if (dynamicOffsets && dynamicOffsetIndex < dynamicOffsetCount)
            {
                uint32_t offset = dynamicOffsets[dynamicOffsetIndex++];
                // Bind one element. Binding through to the end of a multi-slot
                // buffer makes the range larger than the uniform block.
                GLsizeiptr size = bb.dynamicRange > 0
                    ? static_cast<GLsizeiptr>(bb.dynamicRange)
                    : static_cast<GLsizeiptr>(glBuf->GetSize() - offset);

                if (bb.isStorage)
                {
                    GLCall(glBindBufferRange(GL_SHADER_STORAGE_BUFFER, bb.slot, glBuf->GetGLID(), offset, size));
                }
                else
                {
                    GLCall(glBindBufferRange(GL_UNIFORM_BUFFER, bb.slot, glBuf->GetGLID(), offset, size));
                }
            }
            else
            {
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
}

void GLDescriptorSet::Reset()
{
    m_Textures.clear();
    m_Cubemaps.clear();
    m_StorageImages.clear();
    m_Buffers.clear();
}
