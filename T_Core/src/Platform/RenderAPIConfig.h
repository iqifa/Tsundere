#pragma once

// Compile-time graphics backend selection without including any RHI or backend
// headers. Use this header from shared dependency headers that only need the
// active backend macro.
#if defined(Vulkan_For_Render)
    #define RenderAPI_Vulkan
#elif defined(OpenGL_For_Render) || !defined(Vulkan_For_Render)
    #define RenderAPI_OpenGL
#else
    #error Only Support OpenGL or Vulkan!
#endif
