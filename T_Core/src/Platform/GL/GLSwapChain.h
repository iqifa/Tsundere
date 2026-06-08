#pragma once

#include "Platform/RHI/RHISwapChain.h"
#include "Core/Core.h"

struct GLFWwindow;

// GL implementation of RHISwapChain — thin wrapper around the GLFW window.
// In GL, there is no explicit swapchain; glfwSwapBuffers handles presentation.
// This class exists for API symmetry with Vulkan.
class T_API GLSwapChain : public RHISwapChain
{
public:
    explicit GLSwapChain(GLFWwindow* window);
    ~GLSwapChain() override = default;

    uint32_t GetWidth() const override;
    uint32_t GetHeight() const override;
    Format GetFormat() const override;
    void Resize(uint32_t w, uint32_t h) override;

private:
    GLFWwindow* m_Window = nullptr;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
};

// Factory — compile-time binding
inline Ref<RHISwapChain> RHISwapChain::Create(GLFWwindow* window)
{
    return CreateRef<GLSwapChain>(window);
}
