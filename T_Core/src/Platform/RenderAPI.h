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
    // Vulkan backend — future, not yet implemented
    #error Vulkan backend not yet implemented!
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

// Include GL backend implementations
#ifdef RenderAPI_OpenGL
    #include "Platform/GL/GLBuffer.h"
    #include "Platform/GL/GLCommandBuffer.h"
    #include "Platform/GL/GLContext.h"
#endif

#pragma warning(pop)
