#include "GLCommandBuffer.h"
#include "GLBuffer.h"
#include "GLFramebuffer.h"
#include "GLPipeline.h"
#include "GLDescriptorSet.h"
#include "Renderer.h"   // GLCall, GLClearError, GLLogCall, ASSERT macros
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

void GLCommandBuffer::EndRenderPass()
{
    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void GLCommandBuffer::BindPipeline(Ref<RHIPipeline> pipeline)
{
    if (pipeline)
        pipeline->Bind();
}

void GLCommandBuffer::BindVertexBuffer(Ref<RHIBuffer> vb, uint32_t binding)
{
    if (vb)
    {
        auto* glBuf = static_cast<GLBuffer*>(vb.get());
        if (glBuf) glBuf->Bind();
    }
    (void)binding;  // GL uses the currently-bound buffer
}

void GLCommandBuffer::BindIndexBuffer(Ref<RHIBuffer> ib)
{
    if (ib)
    {
        auto* glBuf = static_cast<GLBuffer*>(ib.get());
        if (glBuf) glBuf->Bind();
    }
}

void GLCommandBuffer::BindDescriptorSet(Ref<RHIDescriptorSet> set, uint32_t slot)
{
    if (set)
        set->Apply(slot);
}

void GLCommandBuffer::Draw(uint32_t vertexCount, uint32_t firstVertex)
{
    GLCall(glDrawArrays(GL_TRIANGLES, firstVertex, vertexCount));
}

void GLCommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t firstIndex)
{
    GLCall(glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT,
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

void GLCommandBuffer::MemoryBarrier()
{
    GLCall(glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT));
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
