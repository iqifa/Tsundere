#include "GLSwapChain.h"
#include <GLFW/glfw3.h>

GLSwapChain::GLSwapChain(GLFWwindow* window)
    : m_Window(window)
{
    if (m_Window)
    {
        glfwGetFramebufferSize(m_Window, (int*)&m_Width, (int*)&m_Height);
    }
}

uint32_t GLSwapChain::GetWidth() const
{
    return m_Width;
}

uint32_t GLSwapChain::GetHeight() const
{
    return m_Height;
}

Format GLSwapChain::GetFormat() const
{
    // GL default framebuffer is always RGBA8 (window system provides the format)
    return Format::RGBA8_UNORM;
}

void GLSwapChain::Resize(uint32_t w, uint32_t h)
{
    m_Width = w;
    m_Height = h;
    // GL handles resize implicitly via glViewport — no swapchain recreation needed
}
