#pragma once

#include "RHITypes.h"
#include "Core/Core.h"
#include "HeadLine.h"
#include <vector>

struct FramebufferAttachmentDesc
{
    Format format = Format::RGBA8_UNORM;
    uint32_t samples = 1;                          // 1 = no MSAA, >1 = MSAA
};

struct FramebufferDesc
{
    uint32_t width = 1080, height = 960;
    std::vector<FramebufferAttachmentDesc> colorAttachments;  // at least 1
    bool hasDepthStencil = true;
    uint32_t samples = 1;                          // MSAA sample count (applies to all attachments)
};

class T_API RHIFramebuffer
{
public:
    virtual ~RHIFramebuffer() = default;

    virtual void Bind() = 0;
    virtual void Unbind() = 0;
    virtual void Resize(uint32_t w, uint32_t h) = 0;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    // Per-attachment native handles (for ImGui display, interop)
    virtual uintptr_t GetColorAttachmentID(uint32_t index = 0) const = 0;
    virtual uintptr_t GetDepthAttachmentID() const = 0;
    virtual uintptr_t GetFramebufferID() const = 0;

    // MSAA resolve: blit from this (MSAA) to dst (non-MSAA)
    // Default: no-op (for non-MSAA framebuffers)
    virtual void ResolveTo(Ref<RHIFramebuffer> dst) {}

    // Number of color attachments
    virtual uint32_t GetColorAttachmentCount() const = 0;

    static Ref<RHIFramebuffer> Create(const FramebufferDesc& desc);
};
