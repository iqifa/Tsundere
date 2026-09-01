#pragma once

// Compile-time graphics backend selection without including any RHI or backend
// headers. Use this header from shared dependency headers that only need the
// active backend macro.
#if defined(Vulkan_For_Render) && defined(OpenGL_For_Render)
    #error Vulkan_For_Render and OpenGL_For_Render cannot both be defined
#elif defined(Vulkan_For_Render)
    #define RenderAPI_Vulkan
#elif defined(OpenGL_For_Render)
    #define RenderAPI_OpenGL
#else
    #error A rendering backend must be selected
#endif