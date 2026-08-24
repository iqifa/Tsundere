#pragma once

#include "Platform/RHI/RHIContext.h"

// GL implementation of RHIContext.
// Wraps GLFW window + GLEW initialization, frame begin/end (clear + swap).
class T_API GLContext : public RHIContext
{
public:
    explicit GLContext(GLFWwindow* window);
    ~GLContext() override;

    void Init(GLFWwindow* window) override;
    void Shutdown() override;

    void BeginFrame() override;
    void EndFrame() override;
    void OnResize(uint32_t w, uint32_t h) override;
    void WaitIdle() override;

    Ref<RHISwapChain> GetSwapChain() override;
    Ref<RHICommandBuffer> GetCommandBuffer() override;
    const RHIFrameContext& GetCurrentFrame() const override;

private:
    GLFWwindow* m_Window = nullptr;
    Ref<RHICommandBuffer> m_CommandBuffer;
    Ref<RHISwapChain> m_SwapChain;   // Placeholder until GLSwapChain (Chunk 4)
    RHIFrameContext m_FrameContext;
    bool m_Initialized = false;
};
