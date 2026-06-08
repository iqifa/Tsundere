#pragma once
#ifndef GBUFFER
#define GBUFFER

#include"Core/Core.h"
#include"HeadLine.h"

struct GBufferSpecification
{
	unsigned int Width = 1080, Height = 960;
};

// GBuffer: 5-MRT FBO for deferred rendering
// Attachments: Position(RGB16F), Normal(RGB16F), Albedo(RGBA8), Specular+Shininess(RGBA16F), Velocity(RG16F) + Depth(D24_S8)
class T_API GBuffer
{
public:
	explicit GBuffer(const GBufferSpecification& spec);
	~GBuffer();

	void Bind();
	void UnBind();
	void Resize(unsigned int w, unsigned int h);

	bool IsComplete();

	unsigned int GetFBO()             const { return m_FBO; }
	unsigned int GetPositionTexture() const { return m_PositionTex; }
	unsigned int GetNormalTexture()   const { return m_NormalTex; }
	unsigned int GetAlbedoTexture()   const { return m_AlbedoTex; }
	unsigned int GetSpecularTexture() const { return m_SpecularTex; }
	unsigned int GetVelocityTexture() const { return m_VelocityTex; }
	unsigned int GetDepthTexture()    const { return m_DepthTex; }

	const GBufferSpecification& GetSpec() const { return m_Spec; }

private:
	void CreateAll();
	void DeleteAll();
	unsigned int CreateAttachment(unsigned int internalFormat, unsigned int format,
	                              unsigned int type, unsigned int attachmentSlot,
	                              unsigned int minFilter, unsigned int magFilter);

	unsigned int m_FBO = 0;
	unsigned int m_PositionTex  = 0;
	unsigned int m_NormalTex    = 0;
	unsigned int m_AlbedoTex    = 0;
	unsigned int m_SpecularTex  = 0;
	unsigned int m_VelocityTex  = 0;
	unsigned int m_DepthTex     = 0;
	GBufferSpecification m_Spec;
};

#endif // !GBUFFER
