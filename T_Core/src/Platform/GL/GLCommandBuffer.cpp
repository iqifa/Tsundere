#include "GLCommandBuffer.h"
#include "GLBuffer.h"
#include "Renderer.h"   // GLCall, GLClearError, GLLogCall, ASSERT macros
#include "GL/glew.h"

// --- Stubs for methods that depend on RHI types from later chunks ---
// These will be filled in when GLFramebuffer (Chunk 2), GLPipeline (Chunk 3),
// and GLDescriptorSet (Chunk 4) are created.

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
    // TODO(Chunk 2): when GLFramebuffer exists, bind it via fb->Bind()
    // For now, just clear the currently-bound framebuffer
    GLCall(glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]));
    GLCall(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
}

void GLCommandBuffer::EndRenderPass()
{
    // TODO(Chunk 2): unbind framebuffer via GLFramebuffer
    GLCall(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void GLCommandBuffer::BindPipeline(Ref<RHIPipeline> pipeline)
{
    // TODO(Chunk 3): when GLPipeline exists, call pipeline->Bind()
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
    // TODO(Chunk 4): when GLDescriptorSet exists, call set->Apply(slot)
    (void)set; (void)slot;
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
    // TODO(Chunk 2): blit between GLFramebuffers
    (void)src; (void)dst;
}
