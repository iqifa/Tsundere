#pragma once
#include"Renderer.h"
#include"Debug/Debug.h"
class Texture
{
private:
	unsigned int m_RendererID;
	string m_FilePath;
	unsigned char* m_LocalBuffer;
	int m_Width, m_Height, m_BPP;

public:
	Texture(const string& path);
	~Texture();
	Texture() {}

	void Bind(unsigned int slot=0) const;
	void UnBind() const;

	unsigned int GetTextureID();

	inline int GetWidth()const { return m_Width; }
	inline int GetHeight()const { return m_Height; }
};

