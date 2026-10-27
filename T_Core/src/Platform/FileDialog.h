#pragma once
#include <Windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

inline std::string OpenModelFileDialog()
{
	OPENFILENAMEA ofn = {};
	char szFile[512] = "";
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = glfwGetWin32Window(glfwGetCurrentContext());
	ofn.lpstrFilter = "Model Files\0*.obj;*.fbx;*.gltf;*.glb\0All Files\0*.*\0";
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
	ofn.hwndOwner = glfwGetWin32Window(glfwGetCurrentContext());
	ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr\0All Files\0*.*\0";
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (GetOpenFileNameA(&ofn))
		return szFile;
	return "";
}
