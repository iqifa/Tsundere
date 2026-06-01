#pragma once

#include "Core/Core.h"
#include "HeadLine.h"
#include "RHITypes.h"

// Forward declarations
class RHICommandBuffer;
class RHISwapChain;
struct GLFWwindow;

// Singleton context — owns the device, swap chain, frame management.
// GL backend: wraps GLFW+GLEW init, swaps buffers.
// VK backend: owns VkInstance, VkDevice, VkQueue, swapchain.
class T_API RHIContext
{
public:
    virtual ~RHIContext() = default;

    virtual void Init(GLFWwindow* window) = 0;
    virtual void Shutdown() = 0;

    // Per-frame: called at start/end of each render loop iteration
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    // Called when the window/viewport resizes
    virtual void OnResize(uint32_t w, uint32_t h) = 0;

    virtual Ref<RHISwapChain> GetSwapChain() = 0;

    // Returns a command buffer ready for recording this frame.
    // GL: returns a singleton that executes immediately.
    // VK: returns one from the per-frame pool.
    virtual Ref<RHICommandBuffer> GetCommandBuffer() = 0;

    // Factory — creates the correct backend context
    static Ref<RHIContext> Create(GLFWwindow* window);

    // Singleton accessor (set after Create)
    static Ref<RHIContext>& Get();
};
