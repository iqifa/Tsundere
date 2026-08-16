#pragma once
#include "Pipeline/Passes/PassCommon.h"

class ShadowPass : public RenderPass
{
public:
	bool Enabled = false;
	float LightDistance = 50.0f;

	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();
		m_Shader = GLShader::CreateCompute("D:/Code/C++/Tsundere/res/shaders/ShadowRay.shader");
		CreateShadowMask(m_Spec.Width, m_Spec.Height);
	}

	void BuildBVH(Ref<Scene> scene)
	{
		m_BVHBuilder = CreateRef<BVHBuilder>();
		BVHBuilder::SetActiveInstance(m_BVHBuilder.get());
		m_BVHBuilder->GatherTriangles(scene);
		m_BVHBuilder->BuildBVH(4);
		m_BVHBuilder->UploadToGPU();
		m_BVHBuilder->MarkClean();
	}

	void Execute(Ref<Scene> scene, RenderResources& resources) override
	{
		if (!Enabled)
			return;

		// Rebuild BVH only when scene geometry changed (dirty-flag).
		// Static scene = zero cost. One-frame stale BVH is imperceptible.
		if (m_BVHBuilder && m_BVHBuilder->IsDirty())
		{
			m_BVHBuilder->GatherTriangles(scene);
			m_BVHBuilder->BuildBVH(4);
			m_BVHBuilder->UploadToGPU();
			m_BVHBuilder->MarkClean();
		}

		if (!m_BVHBuilder->GetTriangleBuffer())
			return;

		m_Shader->Bind();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, resources.DepthTexture);
		m_Shader->SetUniform1i("u_DepthTex", 0);

		glBindImageTexture(1, m_ShadowMask, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);

		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 invViewProj = glm::inverse(proj * view);
		m_Shader->SetUniformMat4f("u_InvViewProj", invViewProj);
		m_Shader->SetUniformVec2("u_Resolution", glm::vec2((float)m_Spec.Width, (float)m_Spec.Height));

		m_BVHBuilder->GetTriangleBuffer()->BindToSlot(3);
		m_BVHBuilder->GetBVHNodeBuffer()->BindToSlot(4);

		m_Shader->SetUniformVec3("u_LightDir", m_LightDir);
		m_Shader->SetUniform1f("u_LightDistance", LightDistance);

		unsigned int gx = (m_Spec.Width + 7) / 8;
		unsigned int gy = (m_Spec.Height + 7) / 8;
		m_Shader->DispatchCompute(gx, gy);

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
		glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R8);

		resources.ShadowMask = m_ShadowMask;
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
		CreateShadowMask(w, h);
	}

	// --- RenderGraph path (stage 7) ---
	//
	// A compute pass: it declares no attachments, so the graph binds no
	// framebuffer and simply invokes the lambda. The shadow mask is a
	// graph-owned storage texture (R8, image write) rather than this pass's
	// own m_ShadowMask, which is what lets ShadowApplyPass depend on it
	// through a real resource edge instead of a raw GL id.
	//
	// The GL_SHADER_IMAGE_ACCESS_BARRIER_BIT barrier stays inside the lambda:
	// the graph orders passes but does not yet insert barriers for
	// compute-write -> sampled-read transitions, so the pass must do it.
	//
	// Returns an invalid handle when there is no BVH to trace against, so the
	// caller can skip ShadowApply entirely.
	RGTextureHandle AddToGraph(
		RenderGraph& graph,
		Ref<Scene> scene,
		RGTextureHandle depth,
		uint32_t width,
		uint32_t height)
	{
		RDGTextureDesc desc;
		desc.width  = width;
		desc.height = height;
		desc.format = Format::R8_UNORM;
		// Storage: written via glBindImageTexture. Sampled: read by ShadowApply.
		desc.usage  = TextureUsage::Storage | TextureUsage::Sampled;

		RGTextureHandle shadowMask = graph.CreateTexture(desc, "ShadowPass.Mask");

		graph.AddPass(
			"ShadowRay",
			[depth, shadowMask](RenderGraphPassBuilder& builder)
			{
				builder.ReadTexture(depth, RGAccess::ReadSRV);
				// Not an attachment — an image write from a compute shader.
				builder.WriteTexture(shadowMask, RGAccess::WriteUAV);
			},
			[this, scene, depth, shadowMask, width, height](
				RHICommandBuffer& cmd, RenderGraphResources& resources)
			{
				// Same dirty-flag rebuild as Execute(): a static scene costs nothing.
				if (m_BVHBuilder && m_BVHBuilder->IsDirty())
				{
					m_BVHBuilder->GatherTriangles(scene);
					m_BVHBuilder->BuildBVH(4);
					m_BVHBuilder->UploadToGPU();
					m_BVHBuilder->MarkClean();
				}

				if (!m_BVHBuilder || !m_BVHBuilder->GetTriangleBuffer())
					return;

				RHITexture2D* depthTex = resources.GetTexture(depth);
				RHITexture2D* maskTex  = resources.GetTexture(shadowMask);
				if (!depthTex || !maskTex)
					return;

				m_Shader->Bind();

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, (GLuint)depthTex->GetNativeID());
				m_Shader->SetUniform1i("u_DepthTex", 0);

				glBindImageTexture(1, (GLuint)maskTex->GetNativeID(),
					0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);

				mat4 view = currentcamera->GetViewFront();
				mat4 proj = currentcamera->GetProj();
				m_Shader->SetUniformMat4f("u_InvViewProj", glm::inverse(proj * view));
						m_Shader->SetUniformVec2("u_Resolution",
					glm::vec2((float)width, (float)height));

				m_BVHBuilder->GetTriangleBuffer()->BindToSlot(3);
				m_BVHBuilder->GetBVHNodeBuffer()->BindToSlot(4);

				m_Shader->SetUniformVec3("u_LightDir", m_LightDir);
				m_Shader->SetUniform1f("u_LightDistance", LightDistance);

				m_Shader->DispatchCompute((width + 7) / 8, (height + 7) / 8);

				// Must happen before ShadowApply samples the mask.
				// No glMemoryBarrier here: the graph derives it from
				// WriteUAV -> ReadSRV and issues it before the reading pass,
				// which is where it is actually needed. See DeriveBarrier().
				// Unbinding the image is still ours to do.
				glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R8);
			});

		return shadowMask;
	}

	void SetLightDir(const glm::vec3& dir) { m_LightDir = dir; }
	Ref<BVHBuilder> GetBVHBuilder() const { return m_BVHBuilder; }

private:
	void CreateShadowMask(unsigned int w, unsigned int h)
	{
		if (m_ShadowMask)
			glDeleteTextures(1, &m_ShadowMask);
		glGenTextures(1, &m_ShadowMask);
		glBindTexture(GL_TEXTURE_2D, m_ShadowMask);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	FrameBufferSpecification m_Spec;
	Ref<GLShader> m_Shader;
	Ref<BVHBuilder> m_BVHBuilder;
	unsigned int m_ShadowMask = 0;
	glm::vec3 m_LightDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
};

