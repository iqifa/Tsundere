#pragma once

#include "RHITypes.h"
#include "Core/Core.h"
#include "HeadLine.h"

struct GLFWwindow;

// Swap chain abstraction.
// GL backend: thin wrapper — GetWidth/Height query the window, format is always RGBA8.
// VK backend: manages VkSwapchainKHR, image views, acquire/present.
class T_API RHISwapChain
{
public:
    virtual ~RHISwapChain() = default;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual Format GetFormat() const = 0;

    // Recreate swapchain on window resize (VK: critical; GL: mostly no-op)
    virtual void Resize(uint32_t w, uint32_t h) = 0;

    static Ref<RHISwapChain> Create(GLFWwindow* window);
};
