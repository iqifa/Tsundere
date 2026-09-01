#pragma once

#include "Core/Core.h"
#include "HeadLine.h"
#include<Debug/Debug.h>
#include<Platform/RHI/RHIContext.h>
#include<Platform/RHI/RHICommandBuffer.h>
#include<Platform/RHI/RHISwapChain.h>
class RHIContext;
class RHICommandBuffer;
class RHISwapChain;
struct GLFWwindow;

// Top-level renderer facade.
// Owns the RHI context + swap chain, provides per-frame command buffers.
// Replaces the scattered renderer.Clear() + m_Window->OnUpdate() pattern
// with a unified BeginFrame → (record commands) → EndFrame flow.
//
// Usage:
//   RHIRenderer::Init(window);
//   while (running) {
//       RHIRenderer::BeginFrame();
//       auto cmd = RHIRenderer::GetCmd();
//       // ... record draw calls via cmd->DrawIndexed() etc. ...
//       RHIRenderer::EndFrame();
//   }
//   RHIRenderer::Shutdown();
class T_API RHIRenderer
{
public:
    // One-time init: creates context + swap chain
    static void Init(GLFWwindow* window);

    // Shutdown: destroys context
    static void Shutdown();

    // Per-frame: begin (acquire swapchain / clear), end (present / swap)
    static void BeginFrame();
    static void EndFrame();

    // Get the per-frame command buffer for recording draw calls
    static Ref<RHICommandBuffer> GetCmd();

    // Access the RHI context directly (rarely needed)
    static Ref<RHIContext> GetContext();

    // Access the swap chain
    static Ref<RHISwapChain> GetSwapChain();

    // Called on window resize
    static void OnResize(uint32_t w, uint32_t h);

private:
    static Ref<RHIContext> s_Context;
    static Ref<RHISwapChain> s_SwapChain;
};


inline void RHIRenderer::Init(GLFWwindow* window)
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

inline void RHIRenderer::Shutdown()
{
    s_SwapChain.reset();
    if (s_Context)
        s_Context->Shutdown();
    s_Context.reset();
    Info_Core("RHIRenderer shutdown");
}

inline void RHIRenderer::BeginFrame()
{
    if (s_Context)
        s_Context->BeginFrame();
}

inline void RHIRenderer::EndFrame()
{
    if (s_Context)
        s_Context->EndFrame();
}

inline Ref<RHICommandBuffer> RHIRenderer::GetCmd()
{
    if (s_Context)
        return s_Context->GetCommandBuffer();
    return nullptr;
}

inline Ref<RHIContext> RHIRenderer::GetContext()
{
    return s_Context;
}

inline Ref<RHISwapChain> RHIRenderer::GetSwapChain()
{
    return s_SwapChain;
}

inline void RHIRenderer::OnResize(uint32_t w, uint32_t h)
{
    if (s_Context)
        s_Context->OnResize(w, h);
    if (s_SwapChain)
        s_SwapChain->Resize(w, h);
}