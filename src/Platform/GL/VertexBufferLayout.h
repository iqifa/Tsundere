#pragma once

#include<vector>
#include"Renderer.h"
#include"ExternalFiles.h"
using namespace std;
struct  VertexBufferElement
{
	int count;
	unsigned int type;
	unsigned char normalized;

	static unsigned int GetSizeOfType(unsigned int type)
	{
		switch (type)
		{
		case GL_FLOAT:		   return 4;
		case GL_UNSIGNED_INT:  return 4;
		case GL_INT:		   return 4;
		case GL_UNSIGNED_BYTE: return 1;
		case GL_FLOAT_VEC3:	   return 3;
		case GL_FLOAT_VEC2:	   return 2;
		}
		ASSERT(false);
		return 0;
	}
	/*template<class T>
	static unsigned int GetGLType(T)
	{
		switch (T)
		{
		case float:			return GL_FLOAT;
		case unsigned int:	return GL_UNSIGNED_INT;
		case unsigned char: return GL_UNSIGNED_BYTE;
		}
		return 0;
	}*/
};

class VertexBufferLayout
{
private:
	vector<VertexBufferElement> m_Elements;
	unsigned int m_Stride;
public:
	VertexBufferLayout() :m_Stride(0) {};

	/*template<typename T>
	void Push2(int count,  T)
	{
		unsigned int GLType = VertexBufferElement::GetGLType(T);

		m_Elements.push_back({ count,GLType,GL_FALSE });
		m_Stride += VertexBufferElement::GetSizeOfType(GLType) * count;
	}*/

	template<typename T>
	void Push(int count)
	{
		static_assert(false);
	}

	template<>
	void Push<int>(int count)
	{
		m_Elements.push_back({ count,GL_INT,GL_FALSE });
		m_Stride += VertexBufferElement::GetSizeOfType(GL_INT) * count;
	}
	template<>
	void Push<float>(int count)
	{
		m_Elements.push_back({ count,GL_FLOAT,GL_FALSE });
		m_Stride += VertexBufferElement::GetSizeOfType(GL_FLOAT) * count;
	}

	template<>
	void Push<unsigned int >(int count)
	{
		m_Elements.push_back({ count,GL_UNSIGNED_INT,GL_FALSE });
		m_Stride += VertexBufferElement::GetSizeOfType(GL_UNSIGNED_INT) * count;
	}

	template<>
	void Push<unsigned char>(int count)
	{
		m_Elements.push_back({ count,GL_UNSIGNED_BYTE,GL_TRUE });
		m_Stride += VertexBufferElement::GetSizeOfType(GL_UNSIGNED_BYTE) * count;
	}

	template<>
	void Push<vec3>(int count)
	{
		m_Elements.push_back({ count * 3,GL_FLOAT,GL_TRUE });
		m_Stride += VertexBufferElement::GetSizeOfType(GL_FLOAT) * 3 * count;
	}

	template<>
	void Push<vec2>(int count)
	{
		m_Elements.push_back({ count * 2,GL_FLOAT,GL_TRUE });
		m_Stride += VertexBufferElement::GetSizeOfType(GL_FLOAT) * 2 * count;
	}

	inline unsigned int GetStride() const {
		return m_Stride;
	}
	inline const vector<VertexBufferElement> GetElements() const { return  m_Elements; }
};
