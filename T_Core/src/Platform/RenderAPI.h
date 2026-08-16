#pragma once

// RenderAPI — compile-time graphics backend selector
//
// When OpenGL_For_Render is defined: GL backend
// When Vulkan_For_Render is defined:  Vulkan backend (future)
//
// Include this header to get the correct concrete types for the active backend.

#pragma warning(push)
#pragma warning(disable:4005)

// --- Backend selection ---
#if defined(Vulkan_For_Render)
    #define RenderAPI_Vulkan
#elif defined(OpenGL_For_Render) || !defined(Vulkan_For_Render)
    // OpenGL backend (default)
    #define RenderAPI_OpenGL
#else
    #error Only Support OpenGL or Vulkan!
#endif

// Include RHI interfaces (always available, backend-agnostic)
#include "Platform/RHI/RHITypes.h"
#include "Platform/RHI/RHIBuffer.h"
#include "Platform/RHI/RHICommandBuffer.h"
#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHITexture.h"
#include "Platform/RHI/RHIFramebuffer.h"
#include "Platform/RHI/RHIShader.h"
#include "Platform/RHI/RHIPipeline.h"
#include "Platform/RHI/RHIDescriptorSet.h"
#include "Platform/RHI/RHISwapChain.h"
#include "Platform/RHI/RHIRenderer.h"

// Include GL backend implementations
#ifdef RenderAPI_OpenGL
    #include "Platform/GL/GLBuffer.h"
    #include "Platform/GL/GLCommandBuffer.h"
    #include "Platform/GL/GLContext.h"
    #include "Platform/GL/GLTexture2D.h"
    #include "Platform/GL/GLTextureCube.h"
    #include "Platform/GL/GLStorageImage.h"
    #include "Platform/GL/GLFramebuffer.h"
    #include "Platform/GL/GLPipeline.h"
    #include "Platform/GL/GLDescriptorSet.h"
    #include "Platform/GL/GLSwapChain.h"
    #include"Platform/Windows/GLWindow.h"
#endif

// Include Vulkan backend implementations
#ifdef RenderAPI_Vulkan




#endif 


#pragma warning(pop)
