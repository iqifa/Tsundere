@echo off
setlocal
set "TEST_RENDERER=%~1"
if not defined TEST_RENDERER set "TEST_RENDERER=Vulkan_For_Render"
rem Run from the repository root in an x64 Visual Studio developer prompt, after building T_Core.
if not exist build mkdir build
if not exist bin mkdir bin
cl /utf-8 /nologo /std:c++17 /EHsc /MDd /Z7 /DT_PLATFORM_WINDOWS /D%TEST_RENDERER% /I T_Core/src /I T_Core/src/Core /I T_Core/vender /I Dependence/include /I T_Tool/include /I T_UI/include /I "%VULKAN_SDK%/Include" tests/ActivationTests.cpp /Fo:build/ActivationTests.obj /Fe:bin/ActivationTests.exe /link /LIBPATH:bin T_Core.lib
if errorlevel 1 exit /b 1
bin\ActivationTests.exe
