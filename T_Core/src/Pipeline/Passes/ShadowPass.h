#pragma once
#include "Pipeline/Passes/PassCommon.h"

class ShadowPass : public RenderPass
{
public:
	bool Enabled = false;
	float LightDistance = 50.0f;

	void Init(Ref<RHIFramebuffer> fb) override
	{
		m_Spec = { fb->GetWidth(), fb->GetHeight() };
		m_Shader = RHIShader::CreateCompute("D:/Code/C++/Tsundere/res/shaders/ShadowRay.shader");
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

		auto cmd = RHIRenderer::GetCmd();
		cmd->BindTexture2D(0, resources.DepthTexture);

		m_ShadowMask->BindAsImage(1, ImageAccess::WriteOnly);

		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 invViewProj = glm::inverse(proj * view);

		// PerPass_ShadowRay UBO. Struct declared in class scope.
		ShadowRayUBO ubo;
		ubo.u_InvViewProj   = invViewProj;
		ubo.u_CameraPos     = glm::vec4(currentcamera->getpos(), 0.0f);
		ubo.u_Resolution    = glm::vec2((float)m_Spec.Width, (float)m_Spec.Height);
		ubo.u_LightDir      = glm::vec4(m_LightDir, 0.0f);
		ubo.u_LightDistance = LightDistance;
		if (!m_ShadowRayUBO)
			m_ShadowRayUBO = RHIBuffer::Create(BufferDesc{ sizeof(ubo), BufferUsage::Uniform, true, nullptr });
		m_ShadowRayUBO->Upload(&ubo, sizeof(ubo));

		m_ShadowDescriptorSet->Reset();
		m_ShadowDescriptorSet->BindUniformBuffer(0, m_ShadowRayUBO);
		if (m_BVHBuilder)
		{
			if (auto t = m_BVHBuilder->GetTriangleBuffer()) m_ShadowDescriptorSet->BindStorageBuffer(3, t);
			if (auto b = m_BVHBuilder->GetBVHNodeBuffer())  m_ShadowDescriptorSet->BindStorageBuffer(4, b);
		}
		m_ShadowDescriptorSet->Apply(0);

		unsigned int gx = (m_Spec.Width + 7) / 8;
		unsigned int gy = (m_Spec.Height + 7) / 8;
		m_Shader->DispatchCompute(gx, gy);

		cmd->ResourceBarrier(BarrierFlags::ShaderImage | BarrierFlags::TextureFetch);
		m_ShadowMask->UnbindAsImage(1);

		resources.ShadowMask = (unsigned int)m_ShadowMask->GetNativeID();
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

				cmd.BindTexture2D(0, depthTex->GetNativeID());
				m_Shader->SetUniform1i("u_DepthTex", 0);

				maskTex->BindAsImage(1, ImageAccess::WriteOnly);

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
				// No barrier here: the graph derives it from WriteUAV -> ReadSRV
				// and issues it before the reading pass, which is where it is
				// actually needed. See DeriveBarrier().
				// Unbinding the image is still ours to do.
				maskTex->UnbindAsImage(1);
			});

		return shadowMask;
	}

	void SetLightDir(const glm::vec3& dir) { m_LightDir = dir; }
	Ref<BVHBuilder> GetBVHBuilder() const { return m_BVHBuilder; }

private:
	void CreateShadowMask(unsigned int w, unsigned int h)
	{
		m_ShadowMask = RHIStorageImage::Create(StorageImageDesc{ w, h, Format::R8_UNORM });
	}

	FrameBufferSpecification m_Spec;
	Ref<RHIShader> m_Shader;
	Ref<BVHBuilder> m_BVHBuilder;
	Ref<RHIStorageImage> m_ShadowMask;
	glm::vec3 m_LightDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));

	// Per-pass UBO + descriptor set.
	struct ShadowRayUBO
	{
		glm::mat4 u_InvViewProj;     //   0
		glm::vec4 u_CameraPos;       //  64 (vec3 → vec4)
		glm::vec2 u_Resolution;      //  80
		int32_t   _pad0[2];          //  88..95  — std140 pad: vec4 needs 16-byte align
		glm::vec4 u_LightDir;        //  96 (vec3 → vec4)
		float     u_LightDistance;   // 112
		// pad 4B  → 116 total
	};
	static_assert(sizeof(ShadowRayUBO) == 116, "ShadowRayUBO must match std140 PerPass_ShadowRay");
	Ref<RHIBuffer> m_ShadowRayUBO;
	Ref<RHIDescriptorSet> m_ShadowDescriptorSet = RHIDescriptorSet::Create();
};

