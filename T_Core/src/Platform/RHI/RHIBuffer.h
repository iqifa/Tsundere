#pragma once

#include "RHITypes.h"
#include "Core/Core.h"
#include "HeadLine.h"

struct BufferDesc
{
    uint32_t size = 0;               // bytes
    BufferUsage usage = BufferUsage::Vertex;
    bool cpuAccess = false;          // need Map/Unmap?
    const void* initialData = nullptr;
};

class T_API RHIBuffer
{
public:
    virtual ~RHIBuffer() = default;

    // Upload data to GPU buffer (offset in bytes)
    virtual void Upload(const void* data, uint32_t size, uint32_t offset = 0) = 0;

    // Map buffer for CPU read/write (only valid if cpuAccess=true)
    virtual void* Map() = 0;
    virtual void Unmap() = 0;

    virtual uint32_t GetSize() const = 0;

    // Factory — backend selected at compile time
    static Ref<RHIBuffer> Create(const BufferDesc& desc);
};
