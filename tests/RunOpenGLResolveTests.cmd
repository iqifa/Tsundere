@echo off
if not exist build\gl-resolve-tests mkdir build\gl-resolve-tests
cl /nologo /std:c++17 /EHsc /utf-8 /MDd /Z7 /DT_PLATFORM_WINDOWS /DT_BUILD_DLL /DOpenGL_For_Render /I T_Core/vender /I T_Core/src /I T_Core/src/Core /I Dependence/include /I T_Tool/include /I T_UI/include /Fo:build/gl-resolve-tests/ /Fe:bin/OpenGLResolveTests.exe tests/OpenGLResolveTests.cpp T_Core/src/Platform/GL/GLTexture2D.cpp T_Core/src/Platform/GL/GLFramebuffer.cpp T_Core/src/Platform/GL/GLCommandBuffer.cpp T_Core/src/Platform/GL/GLPipeline.cpp T_Core/src/Platform/GL/GLBuffer.cpp T_Core/src/Platform/GL/GLDescriptorSet.cpp /link /LIBPATH:Dependence/lib/GLEW /LIBPATH:Dependence/lib/GLFW glew32.lib glfw3.lib opengl32.lib user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
bin\OpenGLResolveTests.exe
exit /b %errorlevel%
