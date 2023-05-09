#include"FrameBuffer.h"

FrameBuffer::FrameBuffer()
{
	InValidate();
}

void FrameBuffer::Bind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_RenderID);
}
void FrameBuffer::UnBind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBuffer::BindTexture(Texture& tex)
{
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.GetTextureID(), 0);
}


void FrameBuffer::BindRenderBuffer(RenderBufferObject& rbo)
{
	rbo.Bind();
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 800, 600);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo.GetRenderBufferID());
	if (!IsComplete())
		debugerror("Error:FrameBuffer isn't Complete!");
	rbo.UnBind();
}

bool FrameBuffer::IsComplete()
{
	return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

Ref<FrameBuffer> FrameBuffer::Create(const FrameBufferSpecification& spec)
{
	return Ref<FrameBuffer>();
}

void FrameBuffer::InValidate()
{
	glGenFramebuffers(1, &m_RenderID);
	glBindFramebuffer(GL_FRAMEBUFFER, m_RenderID);
	/// <summary>
	/// create and bind color_attachment
	/// </summary>
	//glCreateTextures(GL_TEXTURE_2D, 1, &m_ColorAttachment);
	glGenTextures(1, &m_ColorAttachment);
	glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specfication.Width, m_Specfication.Height, 0, GL_RGBA, GL_UNSIGNED_INT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

	m_rbo = CreateRef<RenderBufferObject>();

	BindRenderBuffer(*m_rbo);

	UnBind();
}
