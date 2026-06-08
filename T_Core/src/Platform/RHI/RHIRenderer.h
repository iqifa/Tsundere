#pragma once

#include "Core/Core.h"
#include "HeadLine.h"

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
