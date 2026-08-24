#pragma once

#include "RHITypes.h"
#include "Core/Core.h"
#include "HeadLine.h"

#include <array>
#include <vector>

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

// Per-color-attachment clear directive.
// enabled == false means load op = Load: previous contents are preserved.
struct ColorClear
{
    bool                enabled = false;
    std::array<float, 4> value  = { 0.0f, 0.0f, 0.0f, 1.0f };
};

// Describes how a render pass begins: which attachments get cleared and to what.
// Mirrors Vulkan's VkRenderPassBeginInfo load-op model.
//
// colorClears is indexed by color attachment slot. A slot beyond the end of the
// vector is not cleared. Per-slot clear values let an MRT pass clear each
// attachment to a different value, which a single glClearColor cannot express.
struct RenderPassBeginInfo
{
    std::vector<ColorClear> colorClears;

    bool  clearDepth      = false;
    float depthClearValue = 1.0f;

    bool     clearStencil      = false;
    uint32_t stencilClearValue = 0;
};

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

    // Load-op-aware variant: binds fb and clears only what info asks for.
    // Used by RenderGraph so pass bodies never touch framebuffer state.
    virtual void BeginRenderPass(Ref<RHIFramebuffer> fb, const RenderPassBeginInfo& info) = 0;

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

    // Blanket barrier — conservative, covers image access + texture fetch.
    // Not named "MemoryBarrier": the Win32 SDK #defines MemoryBarrier to
    // __faststorefence on x64, which would rewrite this token.
    virtual void ResourceBarrier() = 0;

    // Targeted barrier. A render graph derives the flags from the declared
    // accesses of adjacent passes, so only the transitions that actually
    // happened are synchronised instead of a blanket flush every pass.
    virtual void ResourceBarrier(BarrierFlags flags) = 0;

    // --- State ---
    virtual void SetViewport(const Viewport& vp) = 0;
    virtual void SetScissor(const Scissor& sc) = 0;

    // --- Dynamic rasterizer/depth state ---
    // GL applies these immediately; VK maps them to dynamic state
    // (vkCmdSetDepthTestEnable / vkCmdSetDepthCompareOp / vkCmdSetCullMode).
    // Needed because some draw paths (RHIMesh::Draw) bind their own VAO and
    // bypass pipelines, so depth/cull must be settable without one.
    virtual void SetDepthTest(bool enable) = 0;
    virtual void SetDepthFunc(CompareOp op) = 0;
    virtual void SetCullMode(CullMode mode) = 0;
    virtual void SetPointSize(float size) = 0;
    virtual void SetBlendState(bool enable, BlendFactor src = BlendFactor::One, BlendFactor dst = BlendFactor::Zero) = 0;

    // Full-texture copy (same size). GL: glCopyImageSubData; VK: vkCmdCopyImage.
    virtual void CopyTexture(RHITexture2D* src, RHITexture2D* dst) = 0;

    // Bind a 2D texture / cubemap to a sampler slot by its opaque native handle
    // (RHITexture2D::GetNativeID()). A handle of 0 unbinds. The GL backend maps
    // this onto glActiveTexture + glBindTexture; VK binds a descriptor set.
    virtual void BindTexture2D(uint32_t slot, uintptr_t nativeID) = 0;
    virtual void BindTextureCube(uint32_t slot, uintptr_t nativeID) = 0;

    // --- Blit ---
    virtual void BlitDepth(Ref<RHIFramebuffer> src, Ref<RHIFramebuffer> dst) = 0;

    // Factory
    static Ref<RHICommandBuffer> Create();
};
