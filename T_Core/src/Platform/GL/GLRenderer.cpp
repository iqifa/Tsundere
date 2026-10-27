#include "Platform/RHI/RHIRenderer.h"
#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHISwapChain.h"
#include "Platform/RHI/RHICommandBuffer.h"
#include "GLContext.h"
#include "GLSwapChain.h"
#include "Debug/Debug.h"
#include "Platform/RenderAPI.h"  // RenderAPI_OpenGL / RenderAPI_Vulkan

#ifdef RenderAPI_OpenGL
// Static singleton storage

Ref<RHIContext>   RHIRenderer::s_Context;
Ref<RHISwapChain> RHIRenderer::s_SwapChain;
#endif // RenderAPI_OpenGL



