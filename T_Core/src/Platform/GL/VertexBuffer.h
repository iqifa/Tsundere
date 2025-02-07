#pragma once
#include"Core/Core.h"
class T_API VertexBuffer
{
public:
	VertexBuffer(const void* data, unsigned int size);
	~VertexBuffer();
	VertexBuffer(){}
	void Bind() const;
	void UnBind() const;
private:
	unsigned int m_RenderID;
};

