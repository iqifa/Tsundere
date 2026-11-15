@echo off
rem Run from the repository root in an x64 Visual Studio developer prompt.
rem Standalone: compiles Scene/BVH.cpp directly, no engine build or T_Core.lib needed.
if not exist build mkdir build
if not exist bin mkdir bin
cl /nologo /std:c++17 /EHsc /utf-8 /O2 /MD /DT_PLATFORM_WINDOWS /DT_BUILD_DLL /I T_Core/src /I T_Core/vender tests/BVHTests.cpp T_Core/src/Scene/BVH.cpp /Fo:build/ /Fe:bin/BVHTests.exe
if errorlevel 1 exit /b 1
bin\BVHTests.exe
