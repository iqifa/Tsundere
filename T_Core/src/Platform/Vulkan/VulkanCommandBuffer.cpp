#include "VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanFramebuffer.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture2D.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanDescriptorSet.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include "Platform/RHI/RHIContext.h"
#include "Debug/Debug.h"

#include <vector>

#ifdef RenderAPI_Vulkan
#endif

namespace
{
    RHIVulkanContext* Context()
    {
        auto& c = RHIContext::Get();
        return c ? dynamic_cast<RHIVulkanContext*>(c.get()) : nullptr;
    }
    VkCommandBuffer Cmd()
    {
        auto* c = Context();
        return c ? c->GetCurrentVkCommandBuffer() : VK_NULL_HANDLE;
    }
    VkAttachmentLoadOp Load(AttachmentLoadOp op)
    {
        return op == AttachmentLoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR :
               op == AttachmentLoadOp::DontCare ? VK_ATTACHMENT_LOAD_OP_DONT_CARE :
               VK_ATTACHMENT_LOAD_OP_LOAD;
    }
    VkAttachmentStoreOp Store(AttachmentStoreOp op)
    {
        return op == AttachmentStoreOp::DontCare ? VK_ATTACHMENT_STORE_OP_DONT_CARE :
                                                    VK_ATTACHMENT_STORE_OP_STORE;
    }

    bool HasStencil(Format format)
    {
        return format == Format::D24_UNORM_S8_UINT ||
               format == Format::D32_SFLOAT_S8_UINT;
    }

    void GetLayoutAccess(
        VkImageLayout layout,
        VkPipelineStageFlags& stage,
        VkAccessFlags& access)
    {
        switch (layout)
        {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            if (layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
                access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            access = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_GENERAL:
            stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            access = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            access = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_UNDEFINED:
        default:
            stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            access = 0;
            break;
        }
    }

    void TransitionImage(
        VkCommandBuffer command,
        VulkanTexture2D& texture,
        VkImageLayout newLayout)
    {
        const VkImageLayout oldLayout = texture.GetLayout();
        if (oldLayout == newLayout)
            return;

        VkPipelineStageFlags sourceStage = 0;
        VkPipelineStageFlags destinationStage = 0;
        VkAccessFlags sourceAccess = 0;
        VkAccessFlags destinationAccess = 0;
        GetLayoutAccess(oldLayout, sourceStage, sourceAccess);
        GetLayoutAccess(newLayout, destinationStage, destinationAccess);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = sourceAccess;
        barrier.dstAccessMask = destinationAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.GetImage();
        barrier.subresourceRange.aspectMask = texture.GetAspectMask();
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            command,
            sourceStage,
            destinationStage,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);
        texture.SetLayout(newLayout);
    }
}

void VulkanCommandBuffer::Begin() {}
void VulkanCommandBuffer::End() {}
void VulkanCommandBuffer::Submit() {}

void VulkanCommandBuffer::BeginRenderPass(Ref<RHIFramebuffer> fb, const float clear[4])
{
    RenderPassBeginInfo info;
    if (!fb) return;
    info.colorAttachments.resize(fb->GetColorAttachmentCount());
    for (auto& a : info.colorAttachments)
    {
        a.loadOp = AttachmentLoadOp::Clear;
        a.clearValue = { clear[0], clear[1], clear[2], clear[3] };
    }
    info.hasDepth = fb->GetDepthAttachment() != nullptr;
    info.depth.loadOp = AttachmentLoadOp::Clear;
    info.depth.stencilLoadOp = AttachmentLoadOp::Clear;
    BeginRenderPass(fb, info);
}

void VulkanCommandBuffer::BeginRenderPass(Ref<RHIFramebuffer> fb, const RenderPassBeginInfo& info)
{
    VkCommandBuffer command = Cmd();
    if (!fb || command == VK_NULL_HANDLE || m_RenderingActive)
        return;

    if (fb->GetWidth() == 0 || fb->GetHeight() == 0)
    {
        Error_Core("VulkanCommandBuffer: cannot begin rendering with a zero-sized framebuffer");
        return;
    }

    const uint32_t colorAttachmentCount = fb->GetColorAttachmentCount();
    uint32_t sampleCount = 0;
    std::vector<VkRenderingAttachmentInfo> color(colorAttachmentCount);

    for (uint32_t i = 0; i < colorAttachmentCount; ++i)
    {
        auto* texture = dynamic_cast<VulkanTexture2D*>(fb->GetColorAttachment(i));
        if (!texture || texture->GetImageView() == VK_NULL_HANDLE)
        {
            Error_Core("VulkanCommandBuffer: color attachment {} is not a valid Vulkan texture", i);
            return;
        }
        if (texture->GetWidth() != fb->GetWidth() ||
            texture->GetHeight() != fb->GetHeight() ||
            (sampleCount != 0 && texture->GetSampleCount() != sampleCount))
        {
            Error_Core("VulkanCommandBuffer: color attachment {} does not match framebuffer extent/sample count", i);
            return;
        }
        if (sampleCount == 0)
            sampleCount = texture->GetSampleCount();

        const auto* src = i < info.colorAttachments.size()
            ? &info.colorAttachments[i]
            : nullptr;
        TransitionImage(command, *texture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        color[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color[i].imageView = texture->GetImageView();
        color[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color[i].loadOp = src ? Load(src->loadOp) : VK_ATTACHMENT_LOAD_OP_LOAD;
        color[i].storeOp = src ? Store(src->storeOp) : VK_ATTACHMENT_STORE_OP_STORE;
        if (src)
        {
            color[i].clearValue.color = {{
                src->clearValue[0],
                src->clearValue[1],
                src->clearValue[2],
                src->clearValue[3] }};
        }
    }

    auto* depthTexture = info.hasDepth
        ? dynamic_cast<VulkanTexture2D*>(fb->GetDepthAttachment())
        : nullptr;
    if (info.hasDepth && !depthTexture)
    {
        Error_Core("VulkanCommandBuffer: render pass declares depth but the framebuffer has no Vulkan depth attachment");
        return;
    }

    if (info.hasDepth && info.depth.readOnly &&
        info.depth.loadOp == AttachmentLoadOp::Clear)
    {
        Error_Core("VulkanCommandBuffer: a read-only depth attachment cannot use load-op Clear");
        return;
    }

    VkRenderingAttachmentInfo depth{};
    VkRenderingAttachmentInfo stencil{};
    const bool hasDepth = depthTexture != nullptr;
    const Format depthFormat = fb->GetDepthAttachmentFormat();
    const bool hasStencil = hasDepth && HasStencil(depthFormat);
    if (hasDepth)
    {
        if (depthTexture->GetWidth() != fb->GetWidth() ||
            depthTexture->GetHeight() != fb->GetHeight() ||
            (sampleCount != 0 && depthTexture->GetSampleCount() != sampleCount))
        {
            Error_Core("VulkanCommandBuffer: depth attachment does not match framebuffer extent/sample count");
            return;
        }
        if (sampleCount == 0)
            sampleCount = depthTexture->GetSampleCount();

        const VkImageLayout depthLayout = info.depth.readOnly
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        TransitionImage(command, *depthTexture, depthLayout);

        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = depthTexture->GetImageView();
        depth.imageLayout = depthLayout;
        depth.loadOp = Load(info.depth.loadOp);
        depth.storeOp = Store(info.depth.storeOp);
        depth.clearValue.depthStencil = {
            info.depth.clearDepth,
            info.depth.clearStencil };

        if (hasStencil)
        {
            stencil = depth;
            stencil.loadOp = Load(info.depth.stencilLoadOp);
            stencil.storeOp = Store(info.depth.stencilStoreOp);
        }
    }

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = { fb->GetWidth(), fb->GetHeight() };
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = static_cast<uint32_t>(color.size());
    rendering.pColorAttachments = color.empty() ? nullptr : color.data();
    rendering.pDepthAttachment = hasDepth ? &depth : nullptr;
    rendering.pStencilAttachment = hasStencil ? &stencil : nullptr;
    vkCmdBeginRendering(command, &rendering);
    m_RenderingActive = true;
}

void VulkanCommandBuffer::EndRenderPass()
{
    VkCommandBuffer command = Cmd();
    if (m_RenderingActive && command != VK_NULL_HANDLE)
        vkCmdEndRendering(command);
    m_RenderingActive = false;
}

void VulkanCommandBuffer::BindPipeline(Ref<RHIPipeline> pipeline)
{
    auto* p = pipeline ? dynamic_cast<VulkanPipeline*>(pipeline.get()) : nullptr;
    VkCommandBuffer command = Cmd();
    if (!p || command == VK_NULL_HANDLE) return;
    p->Bind();
    m_CurrentPipeline = p->GetPipeline();
    m_CurrentPipelineLayout = p->GetPipelineLayout();
}

void VulkanCommandBuffer::BindVertexBuffer(Ref<RHIBuffer> vb, uint32_t binding)
{
    auto* b = vb ? dynamic_cast<VulkanBuffer*>(vb.get()) : nullptr;
    VkCommandBuffer command = Cmd();
    if (!b || command == VK_NULL_HANDLE) return;
    VkBuffer buffers[] = { b->GetBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command, binding, 1, buffers, offsets);
}

void VulkanCommandBuffer::BindIndexBuffer(Ref<RHIBuffer> ib)
{
    auto* b = ib ? dynamic_cast<VulkanBuffer*>(ib.get()) : nullptr;
    VkCommandBuffer command = Cmd();
    if (!b || command == VK_NULL_HANDLE) return;
    vkCmdBindIndexBuffer(command, b->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
    m_CurrentIndexBuffer = b->GetBuffer();
}

void VulkanCommandBuffer::BindDescriptorSet(
    Ref<RHIDescriptorSet> set,
    uint32_t slot)
{
    auto* descriptorSet = set
        ? dynamic_cast<VulkanDescriptorSet*>(set.get())
        : nullptr;
    VkCommandBuffer command = Cmd();
    if (!descriptorSet ||
        command == VK_NULL_HANDLE ||
        m_CurrentPipelineLayout == VK_NULL_HANDLE)
    {
        return;
    }

    const VkDescriptorSet nativeSet = descriptorSet->GetDescriptorSet();
    if (nativeSet == VK_NULL_HANDLE)
        return;

    vkCmdBindDescriptorSets(
        command,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_CurrentPipelineLayout,
        slot,
        1,
        &nativeSet,
        0,
        nullptr);
}
void VulkanCommandBuffer::Draw(uint32_t count, uint32_t first) { if (Cmd()) vkCmdDraw(Cmd(), count, 1, first, 0); }
void VulkanCommandBuffer::DrawIndexed(uint32_t count, uint32_t first) { if (Cmd() && m_CurrentIndexBuffer) vkCmdDrawIndexed(Cmd(), count, 1, first, 0, 0); }
void VulkanCommandBuffer::DrawFullscreenQuad() { Draw(3, 0); }
void VulkanCommandBuffer::Dispatch(uint32_t x, uint32_t y, uint32_t z) { if (Cmd()) vkCmdDispatch(Cmd(), x, y, z); }
void VulkanCommandBuffer::ResourceBarrier() {}
void VulkanCommandBuffer::ResourceBarrier(BarrierFlags) {}

void VulkanCommandBuffer::SetViewport(const Viewport& vp)
{
    if (!Cmd()) return;
    VkViewport value{ vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth };
    vkCmdSetViewport(Cmd(), 0, 1, &value);
}
void VulkanCommandBuffer::SetScissor(const Scissor& sc)
{
    if (!Cmd()) return;
    VkRect2D value{ { sc.x, sc.y }, { sc.width, sc.height } };
    vkCmdSetScissor(Cmd(), 0, 1, &value);
}
void VulkanCommandBuffer::SetDepthTest(bool) {}
void VulkanCommandBuffer::SetDepthFunc(CompareOp) {}
void VulkanCommandBuffer::SetCullMode(CullMode) {}
void VulkanCommandBuffer::SetPointSize(float) {}
void VulkanCommandBuffer::SetBlendState(bool, BlendFactor, BlendFactor) {}
void VulkanCommandBuffer::CopyTexture(RHITexture2D*, RHITexture2D*) {}

void VulkanCommandBuffer::PrepareTextureForSampling(RHITexture2D* texture)
{
    auto* vulkanTexture = dynamic_cast<VulkanTexture2D*>(texture);
    VkCommandBuffer command = Cmd();
    if (!vulkanTexture || command == VK_NULL_HANDLE)
    {
        Error_Core("VulkanCommandBuffer: cannot prepare invalid texture for sampling");
        return;
    }

    TransitionImage(command, *vulkanTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
void VulkanCommandBuffer::BindTexture2D(uint32_t, uintptr_t) {}
void VulkanCommandBuffer::BindTextureCube(uint32_t, uintptr_t) {}
void VulkanCommandBuffer::BlitDepth(Ref<RHIFramebuffer>, Ref<RHIFramebuffer>) {}

Ref<RHICommandBuffer> RHICommandBuffer::Create()
{
    return CreateRef<VulkanCommandBuffer>();
}
