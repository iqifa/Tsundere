#include "GLCommandBuffer.h"
#include "GLBuffer.h"
#include "GLFramebuffer.h"
#include "GLPipeline.h"
#include "GLDescriptorSet.h"
#include "GLDebug.h"
#include "Platform/RenderAPI.h"  // RenderAPI_OpenGL / RenderAPI_Vulkan
#include "GL/glew.h"

void GLCommandBuffer::Begin()
{
    // GL executes immediately — no begin needed
}

void GLCommandBuffer::End()
{
    // No deferred recording
}

void GLCommandBuffer::Submit()
{
    // Commands were already executed
}

void GLCommandBuffer::BeginRenderPass(Ref<RHIFramebuffer> fb, const float clearColor[4])
{
    if (fb)
    {
        fb->Bind();
    }
    else
    {
        GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    }
    GLCall(glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]));

    // Only clear depth if the FBO has a depth attachment (default FB always has depth)
    GLbitfield clearMask = GL_COLOR_BUFFER_BIT;
    if (!fb || fb->GetDepthAttachmentID() != 0)
        clearMask |= GL_DEPTH_BUFFER_BIT;

    GLCall(glClear(clearMask));
}

void GLCommandBuffer::BeginRenderPass(Ref<RHIFramebuffer> fb, const RenderPassBeginInfo& info)
{
    if (fb)
    {
        fb->Bind();   // also sets the viewport to the FBO size
    }
    else
    {
        GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    }

    // Per-attachment color clear. glClearBufferfv targets one draw buffer at a
    // time, so each MRT slot can get its own clear value — glClearColor+glClear
    // would force every attachment to the same value.
    for (size_t slot = 0; slot < info.colorClears.size(); ++slot)
    {
        const ColorClear& clear = info.colorClears[slot];
        if (!clear.enabled)
            continue;   // load op = Load: keep previous contents

        GLCall(glClearBufferfv(GL_COLOR, static_cast<GLint>(slot), clear.value.data()));
    }

    if (info.clearDepth && info.clearStencil)
    {
        GLCall(glClearBufferfi(GL_DEPTH_STENCIL, 0,
            info.depthClearValue, static_cast<GLint>(info.stencilClearValue)));
    }
    else if (info.clearDepth)
    {
        // Depth writes must be enabled for a depth clear to take effect.
        GLCall(glDepthMask(GL_TRUE));
        GLCall(glClearBufferfv(GL_DEPTH, 0, &info.depthClearValue));
    }
    else if (info.clearStencil)
    {
        const GLint stencil = static_cast<GLint>(info.stencilClearValue);
        GLCall(glClearBufferiv(GL_STENCIL, 0, &stencil));
    }
}

void GLCommandBuffer::EndRenderPass()
{
    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void GLCommandBuffer::BindPipeline(Ref<RHIPipeline> pipeline)
{
    if (pipeline)
    {
        pipeline->Bind();
        m_CurrentPipeline = std::static_pointer_cast<GLPipeline>(pipeline);
    }
    else
    {
        m_CurrentPipeline.reset();
    }
}

void GLCommandBuffer::BindVertexBuffer(Ref<RHIBuffer> vb, uint32_t binding)
{
    if (!vb || !m_CurrentPipeline)
        return;

    auto* glBuf = static_cast<GLBuffer*>(vb.get());
    if (!glBuf) return;

    // Attribute pointers are VAO state, and glVertexAttribPointer captures the
    // currently-bound GL_ARRAY_BUFFER. Re-bind the pipeline's VAO, bind this VB,
    // then re-specify the attribute layout so the VAO references THIS buffer.
    // (Vulkan binds the buffer directly and this re-spec is unnecessary, so the
    //  VK backend just records vkCmdBindVertexBuffers and ignores the layout.)
    m_CurrentPipeline->BindVertexArray();
    GLCall(glBindBuffer(GL_ARRAY_BUFFER, glBuf->GetGLID()));
    GLPipelineUtil::SetupVertexAttributes(m_CurrentPipeline->GetVertexLayout());

    (void)binding;  // GL core profile has no vertex binding index (pre-4.3 model)
}

void GLCommandBuffer::BindIndexBuffer(Ref<RHIBuffer> ib)
{
    if (!ib || !m_CurrentPipeline)
        return;

    auto* glBuf = static_cast<GLBuffer*>(ib.get());
    if (!glBuf) return;

    // The element-array buffer binding is stored in the VAO, so the pipeline's
    // VAO must be current when it is bound.
    m_CurrentPipeline->BindVertexArray();
    GLCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuf->GetGLID()));
}

void GLCommandBuffer::BindDescriptorSet(Ref<RHIDescriptorSet> set, uint32_t slot)
{
    if (set)
        set->Apply(slot);
}

void GLCommandBuffer::Draw(uint32_t vertexCount, uint32_t firstVertex)
{
    unsigned int prim = m_CurrentPipeline
        ? GLPipelineUtil::ToGLPrimitive(m_CurrentPipeline->GetTopology())
        : GL_TRIANGLES;
    GLCall(glDrawArrays(prim, firstVertex, vertexCount));
}

void GLCommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t firstIndex)
{
    unsigned int prim = m_CurrentPipeline
        ? GLPipelineUtil::ToGLPrimitive(m_CurrentPipeline->GetTopology())
        : GL_TRIANGLES;
    GLCall(glDrawElements(prim, indexCount, GL_UNSIGNED_INT,
        reinterpret_cast<void*>(static_cast<uintptr_t>(firstIndex * sizeof(unsigned int)))));
}

void GLCommandBuffer::DrawFullscreenQuad()
{
    // Draw 3 vertices — vertex shader generates fullscreen triangle from gl_VertexID
    GLCall(glDrawArrays(GL_TRIANGLES, 0, 3));
}

void GLCommandBuffer::Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
{
    GLCall(glDispatchCompute(groupsX, groupsY, groupsZ));
}

void GLCommandBuffer::ResourceBarrier()
{
    GLCall(glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT));
}

void GLCommandBuffer::ResourceBarrier(BarrierFlags flags)
{
    if (flags == BarrierFlags::None)
        return;

    GLbitfield bits = 0;
    if (flags & BarrierFlags::ShaderImage)   bits |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
    if (flags & BarrierFlags::TextureFetch)  bits |= GL_TEXTURE_FETCH_BARRIER_BIT;
    if (flags & BarrierFlags::StorageBuffer) bits |= GL_SHADER_STORAGE_BARRIER_BIT;
    if (flags & BarrierFlags::Framebuffer)   bits |= GL_FRAMEBUFFER_BARRIER_BIT;

    if (bits)
        GLCall(glMemoryBarrier(bits));
}

void GLCommandBuffer::SetViewport(const Viewport& vp)
{
    GLCall(glViewport(static_cast<GLint>(vp.x), static_cast<GLint>(vp.y),
        static_cast<GLsizei>(vp.width), static_cast<GLsizei>(vp.height)));
}

void GLCommandBuffer::SetScissor(const Scissor& sc)
{
    GLCall(glScissor(sc.x, sc.y, sc.width, sc.height));
}

void GLCommandBuffer::SetDepthTest(bool enable)
{
    // Braces required: GLCall() expands to multiple statements (incl. an ASSERT
    // with its own if), so a brace-less if/else would bind the else to the
    // macro's internal if.
    if (enable)
    {
        GLCall(glEnable(GL_DEPTH_TEST));
    }
    else
    {
        GLCall(glDisable(GL_DEPTH_TEST));
    }
}

void GLCommandBuffer::SetDepthFunc(CompareOp op)
{
    GLCall(glDepthFunc(GLPipelineUtil::ToGLCompareOp(op)));
}

void GLCommandBuffer::SetCullMode(CullMode mode)
{
    if (mode == CullMode::None)
    {
        GLCall(glDisable(GL_CULL_FACE));
    }
    else
    {
        GLCall(glEnable(GL_CULL_FACE));
        GLCall(glCullFace(GLPipelineUtil::ToGLCullFace(mode)));
    }
}

void GLCommandBuffer::SetPointSize(float size)
{
    GLCall(glPointSize(size));
}

void GLCommandBuffer::SetBlendState(bool enable, BlendFactor src, BlendFactor dst)
{
    if (enable)
    {
        GLCall(glEnable(GL_BLEND));
        GLCall(glBlendFunc(GLPipelineUtil::ToGLBlendFactor(src), GLPipelineUtil::ToGLBlendFactor(dst)));
    }
    else
    {
        GLCall(glDisable(GL_BLEND));
    }
}

void GLCommandBuffer::CopyTexture(RHITexture2D* src, RHITexture2D* dst)
{
    if (!src || !dst) return;

    GLCall(glCopyImageSubData(
        static_cast<unsigned int>(src->GetNativeID()), GL_TEXTURE_2D, 0, 0, 0, 0,
        static_cast<unsigned int>(dst->GetNativeID()), GL_TEXTURE_2D, 0, 0, 0, 0,
        static_cast<GLint>(src->GetWidth()),
        static_cast<GLint>(src->GetHeight()), 1));
}

void GLCommandBuffer::BindTexture2D(uint32_t slot, uintptr_t nativeID)
{
    GLCall(glActiveTexture(GL_TEXTURE0 + slot));
    GLCall(glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(nativeID)));
}

void GLCommandBuffer::BindTextureCube(uint32_t slot, uintptr_t nativeID)
{
    GLCall(glActiveTexture(GL_TEXTURE0 + slot));
    GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<unsigned int>(nativeID)));
}

void GLCommandBuffer::BlitDepth(Ref<RHIFramebuffer> src, Ref<RHIFramebuffer> dst)
{
    if (!src || !dst) return;

    GLCall(glBindFramebuffer(GL_READ_FRAMEBUFFER,
        static_cast<unsigned int>(src->GetFramebufferID())));
    GLCall(glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
        static_cast<unsigned int>(dst->GetFramebufferID())));
    GLCall(glBlitFramebuffer(
        0, 0, src->GetWidth(), src->GetHeight(),
        0, 0, dst->GetWidth(), dst->GetHeight(),
        GL_DEPTH_BUFFER_BIT, GL_NEAREST));
    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

#ifdef RenderAPI_OpenGL
Ref<RHICommandBuffer> RHICommandBuffer::Create()
{
    return CreateRef<GLCommandBuffer>();
}
#endif
