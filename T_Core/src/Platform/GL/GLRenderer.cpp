#include "Platform/RHI/RHIRenderer.h"
#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHISwapChain.h"
#include "Platform/RHI/RHICommandBuffer.h"
#include "GLContext.h"
#include "GLSwapChain.h"
#include "Debug/Debug.h"
#include "Platform/RenderAPI.h"  // RenderAPI_OpenGL / RenderAPI_Vulkan

#ifdef RenderAPI_OpenGL
#endif // RenderAPI_OpenGL

// Static singleton storage
Ref<RHIContext>   RHIRenderer::s_Context;
Ref<RHISwapChain> RHIRenderer::s_SwapChain;

void RHIRenderer::Init(GLFWwindow* window)
{
    if (s_Context)
    {
        Warn_Core("RHIRenderer::Init called twice — ignoring");
        return;
    }

    // Create backend context (GL: wraps GLEW init + GL state setup)
    s_Context = RHIContext::Create(window);
    s_SwapChain = s_Context ? s_Context->GetSwapChain() : nullptr;

    Info_Core("RHIRenderer initialized");
}

void RHIRenderer::Shutdown()
{
    s_SwapChain.reset();
    if (s_Context)
        s_Context->Shutdown();
    s_Context.reset();
    Info_Core("RHIRenderer shutdown");
}

void RHIRenderer::BeginFrame()
{
    if (s_Context)
        s_Context->BeginFrame();
}

void RHIRenderer::EndFrame()
{
    if (s_Context)
        s_Context->EndFrame();
}

Ref<RHICommandBuffer> RHIRenderer::GetCmd()
{
    if (s_Context)
        return s_Context->GetCommandBuffer();
    return nullptr;
}

Ref<RHIContext> RHIRenderer::GetContext()
{
    return s_Context;
}

Ref<RHISwapChain> RHIRenderer::GetSwapChain()
{
    return s_SwapChain;
}

void RHIRenderer::OnResize(uint32_t w, uint32_t h)
{
    if (s_Context)
        s_Context->OnResize(w, h);
    if (s_SwapChain)
        s_SwapChain->Resize(w, h);
}