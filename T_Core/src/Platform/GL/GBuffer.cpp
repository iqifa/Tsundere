#include"GBuffer.h"
#include"Renderer.h"
#include"Debug/Debug.h"

GBuffer::GBuffer(const GBufferSpecification& spec) : m_Spec(spec)
{
	CreateAll();
}

GBuffer::~GBuffer()
{
	DeleteAll();
}

void GBuffer::DeleteAll()
{
	if (m_FBO)           { glDeleteFramebuffers(1, &m_FBO); m_FBO = 0; }
	if (m_PositionTex)   { glDeleteTextures(1, &m_PositionTex); m_PositionTex = 0; }
	if (m_NormalTex)     { glDeleteTextures(1, &m_NormalTex); m_NormalTex = 0; }
	if (m_AlbedoTex)     { glDeleteTextures(1, &m_AlbedoTex); m_AlbedoTex = 0; }
	if (m_SpecularTex)   { glDeleteTextures(1, &m_SpecularTex); m_SpecularTex = 0; }
	if (m_VelocityTex)   { glDeleteTextures(1, &m_VelocityTex); m_VelocityTex = 0; }
	if (m_DepthTex)      { glDeleteTextures(1, &m_DepthTex); m_DepthTex = 0; }
}

unsigned int GBuffer::CreateAttachment(unsigned int internalFormat, unsigned int format,
                                        unsigned int type, unsigned int attachmentSlot,
                                        unsigned int minFilter, unsigned int magFilter)
{
	unsigned int tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Spec.Width, m_Spec.Height, 0, format, type, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentSlot, GL_TEXTURE_2D, tex, 0);
	return tex;
}

void GBuffer::CreateAll()
{
	if (m_FBO)
		DeleteAll();

	glGenFramebuffers(1, &m_FBO);
	glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

	// COLOR_ATTACHMENT0: World-space position (RGB16F)
	m_PositionTex = CreateAttachment(GL_RGB16F, GL_RGB, GL_FLOAT,
	                                 GL_COLOR_ATTACHMENT0, GL_LINEAR, GL_LINEAR);

	// COLOR_ATTACHMENT1: World-space normal (RGB16F)
	m_NormalTex = CreateAttachment(GL_RGB16F, GL_RGB, GL_FLOAT,
	                               GL_COLOR_ATTACHMENT1, GL_LINEAR, GL_LINEAR);

	// COLOR_ATTACHMENT2: Albedo (RGBA8)
	m_AlbedoTex = CreateAttachment(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,
	                               GL_COLOR_ATTACHMENT2, GL_LINEAR, GL_LINEAR);

	// COLOR_ATTACHMENT3: Specular RGB + Shininess A (RGBA16F — float storage for shininess > 1.0)
	m_SpecularTex = CreateAttachment(GL_RGBA16F, GL_RGBA, GL_FLOAT,
	                                 GL_COLOR_ATTACHMENT3, GL_LINEAR, GL_LINEAR);

	// COLOR_ATTACHMENT4: Velocity / Motion Vectors (RG16F)
	m_VelocityTex = CreateAttachment(GL_RG16F, GL_RG, GL_FLOAT,
	                                 GL_COLOR_ATTACHMENT4, GL_LINEAR, GL_LINEAR);

	// Depth-stencil attachment
	glGenTextures(1, &m_DepthTex);
	glBindTexture(GL_TEXTURE_2D, m_DepthTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, m_Spec.Width, m_Spec.Height, 0,
	             GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthTex, 0);

	// Set all 5 draw buffers
	GLenum drawBufs[] = {
		GL_COLOR_ATTACHMENT0,  // Position
		GL_COLOR_ATTACHMENT1,  // Normal
		GL_COLOR_ATTACHMENT2,  // Albedo
		GL_COLOR_ATTACHMENT3,  // Specular
		GL_COLOR_ATTACHMENT4   // Velocity
	};
	glDrawBuffers(5, drawBufs);

	if (!IsComplete())
		Error_Core("Error: GBuffer isn't Complete!");

	UnBind();
}

void GBuffer::Bind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
	glViewport(0, 0, m_Spec.Width, m_Spec.Height);
}

void GBuffer::UnBind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GBuffer::Resize(unsigned int w, unsigned int h)
{
	m_Spec.Width  = w;
	m_Spec.Height = h;
	CreateAll();
}

bool GBuffer::IsComplete()
{
	return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}
