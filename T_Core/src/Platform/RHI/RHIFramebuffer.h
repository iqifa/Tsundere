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

// Legacy viewport-size carrier, moved here from Platform/GL/FrameBuffer.h.
// Passes keep one of these purely to remember the current render-target size
// (Width/Height) across Init/Resize; it is not used to create framebuffers.
struct FrameBufferSpecification
{
    unsigned int Width = 1080, Height = 960;
    unsigned int Samples = 1;
};

// Forward declaration — a view attaches textures owned by someone else.
class RHITexture2D;

// Describes a NON-OWNING framebuffer built from pre-existing textures.
//
// RHIFramebuffer::Create() allocates its own attachment textures. That is wrong
// for a render graph, which allocates textures itself and only needs somewhere
// to bind them. A view owns just the framebuffer object; the attachment textures
// stay owned by the caller (e.g. RenderGraph) and are NOT deleted on destruction.
struct FramebufferViewDesc
{
    std::vector<RHITexture2D*> colorAttachments;   // may be empty (depth-only pass)
    RHITexture2D* depthAttachment = nullptr;       // may be null (color-only pass)

    // Depth attachment point depends on the format: a combined depth-stencil
    // format must bind to GL_DEPTH_STENCIL_ATTACHMENT, plain depth to
    // GL_DEPTH_ATTACHMENT. The caller knows the format, so it passes it here.
    Format depthFormat = Format::D32_SFLOAT;

    uint32_t width  = 0;
    uint32_t height = 0;
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

    // RHI texture objects for the attachments (non-owning — the framebuffer
    // outlives these). Let passes bind attachments via RHITexture2D::Bind
    // instead of the raw GL id. Default nullptr until a backend implements it.
    virtual RHITexture2D* GetColorAttachment(uint32_t index = 0) const { (void)index; return nullptr; }
    virtual RHITexture2D* GetDepthAttachment() const { return nullptr; }

    // MSAA resolve: blit from this (MSAA) to dst (non-MSAA)
    // Default: no-op (for non-MSAA framebuffers)
    virtual void ResolveTo(Ref<RHIFramebuffer> dst) {}

    // Number of color attachments
    virtual uint32_t GetColorAttachmentCount() const = 0;

    // Explicit-rendering metadata. Native backends may override these; the
    // defaults keep legacy framebuffer implementations source-compatible.
    virtual Format GetColorAttachmentFormat(uint32_t index = 0) const
    {
        (void)index;
        return Format::Unknown;
    }
    virtual Format GetDepthAttachmentFormat() const { return Format::Unknown; }
    virtual uint32_t GetSampleCount() const { return 1; }

    static Ref<RHIFramebuffer> Create(const FramebufferDesc& desc);

    // Creates a non-owning framebuffer over externally-owned textures.
    // Destroying the returned object deletes only the framebuffer object,
    // never the attachment textures. See FramebufferViewDesc.
    static Ref<RHIFramebuffer> CreateView(const FramebufferViewDesc& desc);
};
