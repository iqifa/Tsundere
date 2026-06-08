#include "GLContext.h"
#include "GLCommandBuffer.h"
#include "GLSwapChain.h"
#include "Renderer.h"       // GLCall, ASSERT
#include "Debug/Debug.h"    // Info_Core, Error_Core
#include "GL/glew.h"
#include "GLFW/glfw3.h"

// --- Singleton storage ---
static Ref<RHIContext> s_RHIContext;

Ref<RHIContext>& RHIContext::Get()
{
    return s_RHIContext;
}

Ref<RHIContext> RHIContext::Create(GLFWwindow* window)
{
    auto ctx = CreateRef<GLContext>(window);
    ctx->Init(window);
    s_RHIContext = ctx;
    return ctx;
}

// --- GLContext ---

GLContext::GLContext(GLFWwindow* window)
    : m_Window(window)
{
}

GLContext::~GLContext()
{
    Shutdown();
}

void GLContext::Init(GLFWwindow* window)
{
    if (m_Initialized) return;

    m_Window = window;

    // GLEW init (GLFW + context already created by WindowsWindow)
    glfwMakeContextCurrent(m_Window);
    GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        Error_Core("GLEW init failed: {}", (const char*)glewGetErrorString(err));
        return;
    }

    Info_Core("OpenGL Renderer: {}", (const char*)glGetString(GL_RENDERER));
    Info_Core("OpenGL Version: {}", (const char*)glGetString(GL_VERSION));

    // Global GL state
    GLCall(glEnable(GL_DEPTH_TEST));
    GLCall(glEnable(GL_STENCIL_TEST));
    GLCall(glEnable(GL_BLEND));
    GLCall(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    // Create the per-frame command buffer (GL: single reusable, executes immediately)
    m_CommandBuffer = CreateRef<GLCommandBuffer>();

    // Create swap chain (GL: thin wrapper around GLFW window)
    m_SwapChain = RHISwapChain::Create(window);

    m_Initialized = true;
    Info_Core("GLContext initialized");
}

void GLContext::Shutdown()
{
    if (!m_Initialized) return;

    m_CommandBuffer.reset();
    m_SwapChain.reset();
    m_Initialized = false;

    Info_Core("GLContext shutdown");
}

void GLContext::BeginFrame()
{
    if (!m_Initialized) return;

    // Clear the default framebuffer at the start of each frame
    GLCall(glClearColor(0.0f, 0.0f, 0.0f, 0.0f));
    GLCall(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
}

void GLContext::EndFrame()
{
    if (!m_Initialized || !m_Window) return;

    // Poll events and swap buffers (was m_Window->OnUpdate())
    glfwPollEvents();
    glfwSwapBuffers(m_Window);
}

void GLContext::OnResize(uint32_t w, uint32_t h)
{
    (void)w; (void)h;
    // GL handles viewport resize implicitly via glViewport in BeginRenderPass
    // No need to recreate swapchain like Vulkan
}

Ref<RHISwapChain> GLContext::GetSwapChain()
{
    return m_SwapChain;
}

Ref<RHICommandBuffer> GLContext::GetCommandBuffer()
{
    return m_CommandBuffer;
}
