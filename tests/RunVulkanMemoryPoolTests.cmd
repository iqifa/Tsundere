@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\1.4.357.0"
if not exist build\memory-pool-tests mkdir build\memory-pool-tests
cl /nologo /std:c++17 /EHsc /utf-8 /MDd /D"T_PLATFORM_WINDOWS" /D"IMGUI_API=__declspec(dllimport)" /D"Vulkan_For_Render" /I T_Core/src /I T_Core/vender /I T_Tool/include /I T_UI/include /I Dependence/include /I "%VULKAN_SDK%/Include" tests/VulkanMemoryPoolTests.cpp /Fo:build/memory-pool-tests/ /Fe:bin/VulkanMemoryPoolTests.exe /link bin/T_Core.lib /LIBPATH:"%VULKAN_SDK%/Lib" vulkan-1.lib user32.lib gdi32.lib shell32.lib
if errorlevel 1 exit /b 1
bin\VulkanMemoryPoolTests.exe
exit /b %errorlevel%
