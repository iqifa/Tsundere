#pragma once
#include"VertexBuffer.h"
#include"Core/Core.h"
class T_API VertexBufferLayout;
class T_API VertexArray
{ 
private:
	unsigned int m_RendererID;
	int m_count;
public:
	VertexArray();
	~VertexArray();
	VertexArray(int count);
	VertexArray(const VertexArray&) = default;

	void Bind() const;
	void UnBind() const;
	int GetCount() const;

	void AddBuffer(const VertexBuffer& vb,const VertexBufferLayout layout);
};

