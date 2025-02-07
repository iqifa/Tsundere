#pragma once
#ifndef FRAMEBUFFER
#define FRAMEBUFFER

#ifndef External
#include"ExternalFiles.h"
#endif // !External

#ifndef HEAD
#include"HeadLine.h"
#endif // !Head


#include"Renderer.h"
#include"Texture.h"

struct FrameBufferSpecification {
	unsigned int Width=1080, Height=960;
	unsigned int Samples = 1;
};
class RenderBufferObject;
class T_API FrameBuffer
{
public:
	FrameBuffer(const FrameBufferSpecification& spec):m_Specfication(spec)
	{
		InValidate();
	}
	FrameBuffer();
	~FrameBuffer(){
		glDeleteFramebuffers(1, &m_RenderID);
	}

	void Bind();
	
	void UnBind();

	void BindTexture(Texture& tex);

	void BindRenderBuffer(RenderBufferObject& rbo);
	void Rsetsize(const vec2& size);
	
	bool IsComplete();

	static Ref<FrameBuffer> Create(const FrameBufferSpecification& spec);

	unsigned int GetFrameID() { return m_RenderID; }
	unsigned int GetClolorAttachmentRenderID() { return m_ColorAttachment; }
	FrameBufferSpecification& GetSpecification() { return m_Specfication; }
private:
	void InValidate();
private:
	unsigned int m_RenderID;
	unsigned int m_ColorAttachment;
	Ref<RenderBufferObject> m_rbo;
	FrameBufferSpecification m_Specfication;
};
class RenderBufferObject {
public:
	RenderBufferObject() {
		glGenRenderbuffers(1, &m_RenderID);
	}
	void Bind()
	{
		glBindRenderbuffer(GL_RENDERBUFFER, m_RenderID);
	}
	void UnBind()
	{
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
	}
	unsigned int GetRenderBufferID() { return m_RenderID; }
private: 
	unsigned int m_RenderID;
};
#endif // !FRAMEBUFFER
