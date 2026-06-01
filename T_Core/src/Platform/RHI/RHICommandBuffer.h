#pragma once

#include "RHITypes.h"
#include "Core/Core.h"
#include "HeadLine.h"

// Forward declarations for RHI types (defined in later chunks)
// For now, the concrete types used in command recording must be the
// existing GL classes when building OpenGL, or VK classes for Vulkan.
// We forward-declare here so the interface doesn't depend on concrete types.
class RHIBuffer;
class RHITexture2D;
class RHITextureCube;
class RHIStorageImage;
class RHIFramebuffer;
class RHIShader;
class RHIPipeline;
class RHIDescriptorSet;

class T_API RHICommandBuffer
{
public:
    virtual ~RHICommandBuffer() = default;

    // Begin/end recording (GL: no-op, VK: vkBeginCommandBuffer)
    virtual void Begin() = 0;
    virtual void End() = 0;

    // Submit recorded commands for execution (GL: no-op, VK: vkQueueSubmit)
    virtual void Submit() = 0;

    // --- Render pass ---
    virtual void BeginRenderPass(Ref<RHIFramebuffer> fb, const float clearColor[4]) = 0;
    virtual void EndRenderPass() = 0;

    // --- Pipeline & resources ---
    virtual void BindPipeline(Ref<RHIPipeline> pipeline) = 0;
    virtual void BindVertexBuffer(Ref<RHIBuffer> vb, uint32_t binding = 0) = 0;
    virtual void BindIndexBuffer(Ref<RHIBuffer> ib) = 0;
    virtual void BindDescriptorSet(Ref<RHIDescriptorSet> set, uint32_t slot = 0) = 0;

    // --- Drawing ---
    virtual void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0) = 0;
    virtual void DrawFullscreenQuad() = 0;

    // --- Compute ---
    virtual void Dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;
    virtual void MemoryBarrier() = 0;

    // --- State ---
    virtual void SetViewport(const Viewport& vp) = 0;
    virtual void SetScissor(const Scissor& sc) = 0;

    // --- Blit ---
    virtual void BlitDepth(Ref<RHIFramebuffer> src, Ref<RHIFramebuffer> dst) = 0;

    // Factory
    static Ref<RHICommandBuffer> Create();
};
