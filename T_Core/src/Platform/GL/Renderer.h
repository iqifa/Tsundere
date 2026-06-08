#pragma once

#include<GL/glew.h>
#include<iostream>

#include<HeadLine.h>

#include"Core/Core.h"

#define ueprofifile(...)

class VertexArray;
class IndexBuffer;
class RHIShader;

#define ASSERT(x) if(!(x)) __debugbreak();
#define GLCall(x) GLClearError();\
	x;\
	ASSERT(GLLogCall(#x,__FILE__,__LINE__));


void GLClearError();
bool GLLogCall(const char* function, const char* file, int line);

class T_API Renderer
{
public:
	void DrawElement(const VertexArray& va, const IndexBuffer& ib, const RHIShader& shader) const;
	void DrawArray(const VertexArray& va, const RHIShader& shader)const;
	static void Clear();
	static void Clear_Color();
	static void BeginScene();
	static void EndScene();

	static void Submission(Ref<VertexArray>& vertexArray, Ref<RHIShader>& shader);
};
