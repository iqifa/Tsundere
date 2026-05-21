#pragma once
#include "HeadLine.h"
#include "Core/Core.h"
#include "ExternalFiles.h"

// Lightweight texture for compute shader image load/store.
// Not for stbi-loaded textures — use Texture class for that.
class T_API ImageTexture
{
public:
	ImageTexture(unsigned int w, unsigned int h,
		unsigned int internalFmt = GL_RGBA32F,
		unsigned int format = GL_RGBA,
		unsigned int type = GL_FLOAT);
	~ImageTexture();

	void Bind(unsigned int unit, unsigned int access = GL_READ_WRITE) const;
	void BindAsTexture(unsigned int slot) const;
	void Resize(unsigned int w, unsigned int h);

	unsigned int GetID() const { return m_RendererID; }
	unsigned int GetWidth() const { return m_Width; }
	unsigned int GetHeight() const { return m_Height; }

	static Ref<ImageTexture> Create(unsigned int w, unsigned int h,
		unsigned int internalFmt = GL_RGBA32F);

private:
	void CreateTexture();

	unsigned int m_RendererID = 0;
	unsigned int m_Width = 0;
	unsigned int m_Height = 0;
	unsigned int m_InternalFormat;
	unsigned int m_Format;
	unsigned int m_Type;
};

