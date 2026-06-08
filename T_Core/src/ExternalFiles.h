#pragma once
#ifndef External
#define External
#define NOMINMAX
#include"Windows.h"

#include<GL/glew.h>
#include <GLFW/glfw3.h>

#include<../vender/imgui/imgui.h>
#include<../vender/imgui/imgui_impl_opengl3.h>
#include<../vender/imgui/imgui_impl_glfw.h>
#include<../vender/imgui/imgui_internal.h>
#include<../vender/imgui/imconfig.h>

#include<../vender/glm/glm.hpp>
#include<../vender/glm/gtc/matrix_transform.hpp>


#include<assimp/Importer.hpp>
#include<assimp/scene.h>
#include<assimp/postprocess.h>
#endif // !External