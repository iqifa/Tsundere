#include"FrameBuffer.h"

FrameBuffer::FrameBuffer()
{
	InValidate();
}
FrameBuffer::FrameBuffer(const FrameBufferSpecification& spec, bool autoInit) : m_Specfication(spec)
{
	if (autoInit) {
		InValidate();
	}
}
FrameBuffer::~FrameBuffer() {
	if (m_RenderID) {
		glDeleteFramebuffers(1, &m_RenderID);
		m_RenderID = 0;
	}
	if (m_ColorAttachment) {
		glDeleteTextures(1, &m_ColorAttachment);
		m_ColorAttachment = 0;
	}
	if (m_Depth_StencilAttachment)
	{
		glDeleteTextures(1, &m_Depth_StencilAttachment);
		m_Depth_StencilAttachment = 0;
	}
}
void FrameBuffer::Bind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_RenderID);
	glViewport(0, 0, m_Specfication.Width, m_Specfication.Height);
}
void FrameBuffer::UnBind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBuffer::BindTexture(Texture& tex)
{
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.GetTextureID(), 0);
}


void FrameBuffer::Rsetsize(const vec2& size)
{
	m_Specfication.Width = size.x;
	m_Specfication.Height = size.y;
	InValidate();
}

bool FrameBuffer::IsComplete()
{
	return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

Ref<FrameBuffer> FrameBuffer::Create(const FrameBufferSpecification& spec)
{
	if (spec.Samples > 1) {
		return CreateRef<MsaaFrameBuffer>(spec);
	}
	return CreateRef<FrameBuffer>(spec);
}

void FrameBuffer::InValidate()
{
	if (m_RenderID) {
		glDeleteFramebuffers(1, &m_RenderID);
		glDeleteTextures(1, &m_ColorAttachment);
	}

	glGenFramebuffers(1, &m_RenderID);
	glBindFramebuffer(GL_FRAMEBUFFER, m_RenderID);

	glGenTextures(1, &m_ColorAttachment);

	// 基类只负责标准的 GL_TEXTURE_2D
	glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specfication.Width, m_Specfication.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

	glGenTextures(1, &m_Depth_StencilAttachment);
	glBindTexture(GL_TEXTURE_2D, m_Depth_StencilAttachment);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, m_Specfication.Width, m_Specfication.Height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	// 深度纹理通常使用 GL_NEAREST，避免插值产生错误的深度值
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// 为了防止在边缘采样时越界导致错误，可以设置包装模式为 Clamp To Edge
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_Depth_StencilAttachment, 0);

	if (!IsComplete())
		debugerror("Error:FrameBuffer isn't Complete!");
	UnBind();
}

MsaaFrameBuffer::MsaaFrameBuffer(const FrameBufferSpecification& spec)
	: FrameBuffer(spec, false) // 告诉父类不要调用父类的 InValidate，避免资源浪费
{
	GLint max_samples = 1;
	glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
	if (spec.Samples > max_samples)
	{
		debugwarring("FrameBuffer:Samples_Point maxValue is {},but setvalue is {},now has set as maxvalue", max_samples, spec.Samples);
		m_Specfication.Samples = max_samples;
	}
	InValidate(); // 手动调用自己重写的 MSAA 版本 InValidate
}
void MsaaFrameBuffer::InValidate()
{
	if (m_RenderID) {
		glDeleteFramebuffers(1, &m_RenderID);
		glDeleteTextures(1, &m_ColorAttachment);
	}

	glGenFramebuffers(1, &m_RenderID);
	glBindFramebuffer(GL_FRAMEBUFFER, m_RenderID);

	glGenTextures(1, &m_ColorAttachment);

	// 派生类专门负责 GL_TEXTURE_2D_MULTISAMPLE
	glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_ColorAttachment);
	glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_Specfication.Samples, GL_RGBA8, m_Specfication.Width, m_Specfication.Height, GL_TRUE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, m_ColorAttachment, 0);

	glGenTextures(1, &m_Depth_StencilAttachment);
	glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_Depth_StencilAttachment);
	glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_Specfication.Samples, GL_DEPTH24_STENCIL8, m_Specfication.Width, m_Specfication.Height, GL_TRUE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, m_Depth_StencilAttachment, 0);


	if (!IsComplete())
		debugerror("Error:MsaaFrameBuffer isn't Complete!");

	UnBind();
}