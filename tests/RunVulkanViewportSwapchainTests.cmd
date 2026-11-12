@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\1.4.357.0"
if not exist build\viewport-tests mkdir build\viewport-tests
cl /nologo /std:c++17 /EHsc /utf-8 /MDd /D"IMGUI_API=__declspec(dllimport)" /I T_Core/vender /I Dependence/include /I "%VULKAN_SDK%/Include" tests/VulkanViewportSwapchainTests.cpp /Fo:build/viewport-tests/ /Fe:bin/VulkanViewportSwapchainTests.exe /link bin/T_Core.lib /LIBPATH:"%VULKAN_SDK%/Lib" /LIBPATH:Dependence/lib/GLFW glfw3.lib vulkan-1.lib user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
bin\VulkanViewportSwapchainTests.exe
exit /b %errorlevel%
