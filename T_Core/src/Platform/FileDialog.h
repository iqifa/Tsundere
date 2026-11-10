#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <string>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "Core/Application.h"

// The active GLFW window. Do NOT use glfwGetCurrentContext(): the Vulkan
// backend creates its window with GLFW_NO_API, so there is never a current
// OpenGL context and glfwGetWin32Window(NULL) dereferences a null pointer.
inline GLFWwindow* GetActiveGLFWWindow()
{
	GLFWwindow* window = static_cast<GLFWwindow*>(Engine::Application::Get().GetWindow().GetWindow());
	return window ? window : glfwGetCurrentContext();
}

inline std::string OpenModelFileDialog()
{
	OPENFILENAMEA ofn = {};
	char szFile[512] = "";
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = glfwGetWin32Window(GetActiveGLFWWindow());
	ofn.lpstrFilter = "Model Files\0*.obj;*.fbx;*.gltf;*.glb;*.pmx;*.pmd\0All Files\0*.*\0";
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (GetOpenFileNameA(&ofn))
		return szFile;
	return "";
}

inline std::string OpenTextureFileDialog()
{
	OPENFILENAMEA ofn = {};
	char szFile[512] = "";
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = glfwGetWin32Window(GetActiveGLFWWindow());
	ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr\0All Files\0*.*\0";
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (GetOpenFileNameA(&ofn))
		return szFile;
	return "";
}

// Generic open dialog: filter like "Shader Files\0*.shader\0All Files\0*.*\0"
// or nullptr for "All Files". Returns "" when cancelled.
inline std::string OpenFileDialog(const char* filter = nullptr)
{
	OPENFILENAMEA ofn = {};
	char szFile[512] = "";
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = glfwGetWin32Window(GetActiveGLFWWindow());
	ofn.lpstrFilter = filter ? filter : "All Files\0*.*\0";
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (GetOpenFileNameA(&ofn))
		return szFile;
	return "";
}

// Generic save dialog. Returns "" when cancelled.
inline std::string SaveFileDialog(const char* filter = nullptr, const char* defaultExtension = "", const char* initialDirectory = nullptr)
{
	OPENFILENAMEA ofn = {};
	char szFile[512] = "";
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = glfwGetWin32Window(GetActiveGLFWWindow());
	ofn.lpstrFilter = filter ? filter : "All Files\0*.*\0";
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrDefExt = defaultExtension;
	ofn.lpstrInitialDir = initialDirectory;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (GetSaveFileNameA(&ofn))
		return szFile;
	return "";
}
