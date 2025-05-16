#pragma once
#pragma warning(push)
#pragma warning(disable:4005)
#define RenderAPI

#ifdef OpenGL_For_Render
#define RenderAPI(x) x;
#else 
#error Only Support OpenGL!!!
#endif // OpenGL_For_Render
#pragma warning(pop)