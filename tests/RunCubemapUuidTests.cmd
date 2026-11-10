@echo off
rem Run from the repository root in an x64 Visual Studio developer prompt after a Debug build.
cl /nologo /std:c++17 /EHsc /utf-8 /MDd /DT_PLATFORM_WINDOWS /I T_Core/src /I T_Core/vender /I Dependence/include /I SandBox/src/Editor_Panel tests/CubemapUuidTests.cpp /Fo:build/CubemapUuidTests.obj /Fe:bin/CubemapUuidTests.exe /link bin/T_Core.lib
if errorlevel 1 exit /b 1
bin\CubemapUuidTests.exe
