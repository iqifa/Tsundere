#pragma once

#include "RHITypes.h"
#include "Core/Core.h"
#include "HeadLine.h"

#include <array>
#include <vector>

class RHIBuffer;
class RHITexture2D;
class RHITextureCube;
class RHIStorageImage;
class RHIFramebuffer;
class RHIShader;
class RHIPipeline;
class RHIDescriptorSet;

struct ColorClear
{
    bool enabled = false;
    std::array<float, 4> value = { 0.0f, 0.0f, 0.0f, 1.0f };
};

enum class AttachmentLoadOp : uint8_t
{
    Load,
    Clear,
    DontCare
};

enum class AttachmentStoreOp : uint8_t
{
    Store,
    DontCare
};

struct ColorAttachmentBeginInfo
{
    AttachmentLoadOp loadOp = AttachmentLoadOp::Load;
    AttachmentStoreOp storeOp = AttachmentStoreOp::Store;
    std::array<float, 4> clearValue = { 0.0f, 0.0f, 0.0f, 1.0f };
};

struct DepthAttachmentBeginInfo
{
    AttachmentLoadOp loadOp = AttachmentLoadOp::Load;
    AttachmentStoreOp storeOp = AttachmentStoreOp::Store;
    float clearDepth = 1.0f;
    AttachmentLoadOp stencilLoadOp = AttachmentLoadOp::Load;
    AttachmentStoreOp stencilStoreOp = AttachmentStoreOp::Store;
    uint32_t clearStencil = 0;
    bool readOnly = false;
};

struct RenderPassBeginInfo
{
    std::vector<ColorAttachmentBeginInfo> colorAttachments;
    bool hasDepth = false;
    DepthAttachmentBeginInfo depth;

    // Legacy fields retained for source compatibility.
    std::vector<ColorClear> colorClears;
    bool clearDepth = false;
    float depthClearValue = 1.0f;
    bool clearStencil = false;
    uint32_t stencilClearValue = 0;
};

class T_API RHICommandBuffer
{
public:
    virtual ~RHICommandBuffer() = default;
    virtual void Begin() = 0;
    virtual void End() = 0;
    virtual void Submit() = 0;
    virtual void BeginRenderPass(Ref<RHIFramebuffer> fb, const float clearColor[4]) = 0;
    virtual void BeginRenderPass(Ref<RHIFramebuffer> fb, const RenderPassBeginInfo& info) = 0;
    virtual void EndRenderPass() = 0;
    virtual void BindPipeline(Ref<RHIPipeline> pipeline) = 0;
    virtual void BindVertexBuffer(Ref<RHIBuffer> vb, uint32_t binding = 0) = 0;
    virtual void BindIndexBuffer(Ref<RHIBuffer> ib) = 0;
    virtual void BindDescriptorSet(Ref<RHIDescriptorSet> set, uint32_t slot = 0) = 0;
    virtual void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0) = 0;
    virtual void DrawFullscreenQuad() = 0;
    virtual void Dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;
    virtual void ResourceBarrier() = 0;
    virtual void ResourceBarrier(BarrierFlags flags) = 0;
    virtual void SetViewport(const Viewport& vp) = 0;
    virtual void SetScissor(const Scissor& sc) = 0;
    virtual void SetDepthTest(bool enable) = 0;
    virtual void SetDepthFunc(CompareOp op) = 0;
    virtual void SetCullMode(CullMode mode) = 0;
    virtual void SetPointSize(float size) = 0;
    virtual void SetBlendState(bool enable, BlendFactor src = BlendFactor::One, BlendFactor dst = BlendFactor::Zero) = 0;
    virtual void CopyTexture(RHITexture2D* src, RHITexture2D* dst) = 0;
    virtual void PrepareTextureForSampling(RHITexture2D* texture) { (void)texture; }
    virtual void BindTexture2D(uint32_t slot, uintptr_t nativeID) = 0;
    virtual void BindTextureCube(uint32_t slot, uintptr_t nativeID) = 0;
    virtual void BlitDepth(Ref<RHIFramebuffer> src, Ref<RHIFramebuffer> dst) = 0;
    static Ref<RHICommandBuffer> Create();
};
