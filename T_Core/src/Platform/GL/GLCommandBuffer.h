#pragma once

#include "Platform/RHI/RHICommandBuffer.h"

// GL immediate-mode implementation of RHICommandBuffer.
// Every call executes OpenGL commands immediately — there is no deferred recording.
// This matches OpenGL's execution model while providing the same API as Vulkan.
class T_API GLCommandBuffer : public RHICommandBuffer
{
public:
    GLCommandBuffer() = default;
    ~GLCommandBuffer() override = default;

    void Begin() override;
    void End() override;
    void Submit() override;

    void BeginRenderPass(Ref<RHIFramebuffer> fb, const float clearColor[4]) override;
    void EndRenderPass() override;

    void BindPipeline(Ref<RHIPipeline> pipeline) override;
    void BindVertexBuffer(Ref<RHIBuffer> vb, uint32_t binding = 0) override;
    void BindIndexBuffer(Ref<RHIBuffer> ib) override;
    void BindDescriptorSet(Ref<RHIDescriptorSet> set, uint32_t slot = 0) override;

    void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0) override;
    void DrawFullscreenQuad() override;

    void Dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;
    void MemoryBarrier() override;

    void SetViewport(const Viewport& vp) override;
    void SetScissor(const Scissor& sc) override;

    void BlitDepth(Ref<RHIFramebuffer> src, Ref<RHIFramebuffer> dst) override;
};
