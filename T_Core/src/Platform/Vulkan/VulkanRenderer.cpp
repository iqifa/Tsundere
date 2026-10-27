#include "Platform/RenderAPI.h"  // RenderAPI_OpenGL / RenderAPI_Vulkan

#ifdef RenderAPI_Vulkan
// Static singleton storage

Ref<RHIContext>   RHIRenderer::s_Context;
Ref<RHISwapChain> RHIRenderer::s_SwapChain;
#endif // RenderAPI_Vulkan


