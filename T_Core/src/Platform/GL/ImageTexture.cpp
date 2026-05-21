#include"ImageTexture.h"

ImageTexture::ImageTexture(unsigned int w, unsigned int h,
	unsigned int internalFmt, unsigned int format, unsigned int type)
	: m_Width(w), m_Height(h)
	, m_InternalFormat(internalFmt), m_Format(format), m_Type(type)
{
	CreateTexture();
}

ImageTexture::~ImageTexture()
{
	if (m_RendererID)
		glDeleteTextures(1, &m_RendererID);
}

void ImageTexture::CreateTexture()
{
	if (m_RendererID)
		glDeleteTextures(1, &m_RendererID);

	glGenTextures(1, &m_RendererID);
	glBindTexture(GL_TEXTURE_2D, m_RendererID);
	glTexImage2D(GL_TEXTURE_2D, 0, m_InternalFormat, m_Width, m_Height, 0, m_Format, m_Type, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void ImageTexture::Bind(unsigned int unit, unsigned int access) const
{
	glBindImageTexture(unit, m_RendererID, 0, GL_FALSE, 0, access, m_InternalFormat);
}

void ImageTexture::BindAsTexture(unsigned int slot) const
{
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, m_RendererID);
}

void ImageTexture::Resize(unsigned int w, unsigned int h)
{
	m_Width = w;
	m_Height = h;
	CreateTexture();
}

Ref<ImageTexture> ImageTexture::Create(unsigned int w, unsigned int h, unsigned int internalFmt)
{
	return CreateRef<ImageTexture>(w, h, internalFmt);
}