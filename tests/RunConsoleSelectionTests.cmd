@echo off
rem Run from the repository root in an x64 VS developer prompt after building the OpenGL editor.
cl /nologo /std:c++17 /EHsc /utf-8 /MDd /Z7 /DT_PLATFORM_WINDOWS /DOpenGL_For_Render /DIMGUI_API=__declspec(dllimport) /I T_Core/src /I T_Core/src/Core /I T_Core/vender /I Dependence/include /I T_Tool/include /I T_UI/include tests/ConsoleSelectionTests.cpp /Fo:build/ConsoleSelectionTests.obj /Fe:bin/ConsoleSelectionTests.exe /link /LIBPATH:bin T_Core.lib
if errorlevel 1 exit /b 1
bin\ConsoleSelectionTests.exe
exit /b %errorlevel%
