#pragma once

#include "Platform/RHI/RHICommandBuffer.h"

#include <vulkan/vulkan.h>

class T_API VulkanCommandBuffer : public RHICommandBuffer
{
public:
    VulkanCommandBuffer() = default;
    ~VulkanCommandBuffer() override = default;

    void Begin() override;
    void End() override;
    void Submit() override;

    void BeginRenderPass(Ref<RHIFramebuffer> fb, const float clearColor[4]) override;
    void BeginRenderPass(Ref<RHIFramebuffer> fb, const RenderPassBeginInfo& info) override;
    void EndRenderPass() override;

    void BindPipeline(Ref<RHIPipeline> pipeline) override;
    void BindVertexBuffer(Ref<RHIBuffer> vb, uint32_t binding = 0) override;
    void BindIndexBuffer(Ref<RHIBuffer> ib) override;
    void BindDescriptorSet(Ref<RHIDescriptorSet> set, uint32_t slot = 0) override;

    void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0) override;
    void DrawFullscreenQuad() override;
    void Dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;

    void ResourceBarrier() override;
    void ResourceBarrier(BarrierFlags flags) override;

    void SetViewport(const Viewport& vp) override;
    void SetScissor(const Scissor& sc) override;
    void SetDepthTest(bool enable) override;
    void SetDepthFunc(CompareOp op) override;
    void SetCullMode(CullMode mode) override;
    void SetPointSize(float size) override;
    void SetBlendState(bool enable, BlendFactor src, BlendFactor dst) override;

    void CopyTexture(RHITexture2D* src, RHITexture2D* dst) override;
    void PrepareTextureForSampling(RHITexture2D* texture) override;
    void BindTexture2D(uint32_t slot, uintptr_t nativeID) override;
    void BindTextureCube(uint32_t slot, uintptr_t nativeID) override;
    void BlitDepth(Ref<RHIFramebuffer> src, Ref<RHIFramebuffer> dst) override;

private:
    bool m_RenderingActive = false;
    VkPipeline m_CurrentPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_CurrentPipelineLayout = VK_NULL_HANDLE;
    VkBuffer m_CurrentIndexBuffer = VK_NULL_HANDLE;
};
