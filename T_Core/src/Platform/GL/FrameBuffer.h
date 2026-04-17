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
	unsigned int Width = 1080, Height = 960;
	unsigned int Samples = 1; // 采样数，大于1则开启多重采样(MSAA)
};
class RenderBufferObject;
class T_API FrameBuffer
{
public:
	FrameBuffer(const FrameBufferSpecification& spec) :m_Specfication(spec)
	{
		InValidate();
	}
	FrameBuffer();
	virtual ~FrameBuffer();


	virtual void Bind();
	virtual void UnBind();

	virtual void BindTexture(Texture& tex);
	virtual void BindRenderBuffer(RenderBufferObject& rbo);

	// Rsetsize 内部会调用多态的 InValidate()
	virtual void Rsetsize(const vec2& size);

	bool IsComplete();

	static Ref<FrameBuffer> Create(const FrameBufferSpecification& spec);

	unsigned int GetFrameID() { return m_RenderID; }
	unsigned int GetClolorAttachmentRenderID() { return m_ColorAttachment; }
	FrameBufferSpecification& GetSpecification() { return m_Specfication; }
protected:
	virtual void InValidate();
	FrameBuffer(const FrameBufferSpecification& spec, bool autoInit);
	unsigned int m_RenderID = 0;
	unsigned int m_ColorAttachment = 0;
	Ref<RenderBufferObject> m_rbo;
	FrameBufferSpecification m_Specfication;
};
class RenderBufferObject {
public:
	RenderBufferObject() {
		glGenRenderbuffers(1, &m_RenderID);
	}
	~RenderBufferObject() {
		glDeleteRenderbuffers(1, &m_RenderID);
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


class T_API MsaaFrameBuffer : public FrameBuffer
{
public:
	MsaaFrameBuffer(const FrameBufferSpecification& spec);
	virtual ~MsaaFrameBuffer() = default;

protected:
	// 重写创建逻辑，专用于 MSAA
	virtual void InValidate() override;
};
#endif // !FRAMEBUFFER