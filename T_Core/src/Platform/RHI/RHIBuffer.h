#pragma once

#include "RHITypes.h"
#include "Core/Core.h"
#include "HeadLine.h"
#include <vector>

struct BufferDesc
{
    uint32_t size = 0;               // bytes
    BufferUsage usage = BufferUsage::Vertex;
    bool cpuAccess = false;          // need Map/Unmap?
    const void* initialData = nullptr;
};

class RHIBuffer;

// RHIBuffer::UploadBatch 的一个区段：把 data[0, size) 写到 buffer 的 [offset, offset + size)。
struct BufferUploadRegion
{
    RHIBuffer* buffer = nullptr;
    const void* data = nullptr;
    uint32_t size = 0;
    uint32_t offset = 0;
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

    // Backend-native buffer handle. GL returns GLuint; Vulkan returns VkBuffer.
    // The default keeps older/mock implementations source-compatible.
    virtual uintptr_t GetNativeID() const { return 0; }

    // Bind this buffer to a shader storage / uniform binding slot.
    // GL: glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, id).
    // VK: maps to a descriptor-set write for a storage buffer at that binding.
    // Default no-op so non-storage buffers (vertex/index) don't need it.
    virtual void BindToSlot(uint32_t slot) const { (void)slot; }

    // Factory — backend selected at compile time
    static Ref<RHIBuffer> Create(const BufferDesc& desc);

    // 一次写入多个 buffer 的多个区段（可跨 buffer）。语义同逐个 Upload，但：
    //   Vulkan  设备本地区段合并进一个暂存 buffer，一个命令缓冲、一次提交、一次等待
    //   OpenGL  逐个 glBufferSubData（本身不等待）
    // 区段越界或 buffer 为空时抛 std::invalid_argument，不写任何数据。
    static void UploadBatch(const std::vector<BufferUploadRegion>& regions);
};
