#pragma once

#include "Platform/RHI/RHIBuffer.h"

// GL implementation of RHIBuffer — single class handles vertex, index, uniform, and storage buffers.
// Picks the correct GL target based on BufferDesc::usage.
class T_API GLBuffer : public RHIBuffer
{
public:
    explicit GLBuffer(const BufferDesc& desc);
    ~GLBuffer() override;

    void Upload(const void* data, uint32_t size, uint32_t offset = 0) override;
    void* Map() override;
    void Unmap() override;
    uint32_t GetSize() const override { return m_Size; }

    // GL-specific helpers (used internally by GLCommandBuffer)
    void Bind() const;
    void Unbind() const;
    void BindToSlot(uint32_t slot) const override;  // for SSBO / UBO
    unsigned int GetGLID() const { return m_RendererID; }

private:
    unsigned int GetGLTarget() const;

    unsigned int m_RendererID = 0;
    uint32_t m_Size = 0;
    BufferUsage m_Usage;
    bool m_CpuAccess = false;
};

// Factory — registered in GLBuffer.cpp
inline Ref<RHIBuffer> RHIBuffer::Create(const BufferDesc& desc)
{
    return CreateRef<GLBuffer>(desc);
}
