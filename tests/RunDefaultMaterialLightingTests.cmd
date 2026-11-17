@echo off
rem Run from the repository root in an x64 Visual Studio developer prompt.
if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\1.4.357.0"
set "PATH=%VULKAN_SDK%\Bin;%PATH%"
if not exist build\default-lighting-tests mkdir build\default-lighting-tests
cl /nologo /std:c++17 /EHsc /utf-8 /MDd /Z7 /DT_PLATFORM_WINDOWS /DT_BUILD_DLL /I T_Core/vender /I T_Core/src /I Dependence/include /I "%VULKAN_SDK%/Include" /Fo:build/default-lighting-tests/ /Fe:bin/DefaultMaterialLightingTests.exe tests/DefaultMaterialLightingTests.cpp T_Core/src/Platform/ShaderCompiler.cpp /link /LIBPATH:"%VULKAN_SDK%/Lib" shaderc_shared.lib spirv-cross-c-shared.lib /LIBPATH:Dependence/lib/GLEW /LIBPATH:Dependence/lib/GLFW glew32.lib glfw3.lib opengl32.lib user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
bin\DefaultMaterialLightingTests.exe
exit /b %errorlevel%
