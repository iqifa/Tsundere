#pragma once
#include"Core/Core.h"
class T_API IndexBuffer
{
public:
	IndexBuffer(const unsigned int* data, unsigned int count);
	~IndexBuffer();
	IndexBuffer(){}

	void Bind() const;
	void UnBind()const;

	inline unsigned int GetCount() const { return m_count; }
private:
	unsigned int m_RenderID;
	unsigned int m_count;
};

