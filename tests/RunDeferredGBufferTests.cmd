@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\1.4.357.0"
set "PATH=%VULKAN_SDK%\Bin;%PATH%"
if not exist build\gbuffer-tests mkdir build\gbuffer-tests
if not exist bin mkdir bin
cl /nologo /std:c++17 /EHsc /utf-8 /MDd /DT_PLATFORM_WINDOWS /DT_BUILD_DLL /I T_Core/vender /I T_Core/src /I Dependence/include /I "%VULKAN_SDK%/Include" /Fo:build/gbuffer-tests/ /Fe:bin/DeferredGBufferTests.exe tests/DeferredGBufferTests.cpp T_Core/src/Platform/ShaderCompiler.cpp /link /LIBPATH:"%VULKAN_SDK%/Lib" shaderc_shared.lib spirv-cross-c-shared.lib /LIBPATH:Dependence/lib/GLEW /LIBPATH:Dependence/lib/GLFW glew32.lib glfw3.lib opengl32.lib user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
bin\DeferredGBufferTests.exe
exit /b %errorlevel%
