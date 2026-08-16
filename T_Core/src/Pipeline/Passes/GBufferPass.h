#pragma once
#include "Pipeline/Passes/PassCommon.h"

class  GBufferPass : public RenderPass
{
	Ptr<GBuffer> m_GBuffer;
	Ref<RHIShader> m_GBufferShader;

	int m_FrameCount = 0;
	unsigned int m_DefaultTex = 0;
	mat4 m_PrevViewProjMatrix = mat4(1.0f);

	Ptr<VertexArray>va;
	Ptr<VertexBuffer>vb;
	Ptr<IndexBuffer>ibo;

	GBufferSpecification m_Spec;

public:
	bool EnableJitter = true;

	void Execute(Ref<Scene> scene, RenderResources& resources) override {
		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 currentViewProj = proj * view;

		if (EnableJitter)
			proj = Jittering(proj, m_Spec.Width, m_Spec.Height);

		m_GBuffer->Bind();

		Renderer renderer;
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDisable(GL_BLEND);

		// Ensure correct GL state for GBuffer rendering (inherited state may vary)
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_DefaultTex);

		m_GBufferShader->Bind();
		bool drewSomething = false;

		for (auto [entityID, transform, meshrender] : scene->m_Registry.view<Component::Transform, Component::MeshRender>().each())
		{
			if (meshrender.ModelPath.empty() || meshrender.materials.empty())
				continue;

			Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
			if (!model || model->meshes.empty())
				continue;

			mat4 modelMat = transform.GetTransform();

			for (size_t i = 0; i < model->meshes.size(); i++)
			{
				auto& mesh = model->meshes[i];
				if (!mesh.IsGPUReady())
					continue;

				Ref<Material> mat = i < meshrender.materials.size()
					? meshrender.materials[i]
					: meshrender.materials[0];

				mat->Render(m_GBufferShader);

				m_GBufferShader->SetUniformMat4f("MVP_matrix", proj * view * modelMat);
				m_GBufferShader->SetUniformMat4f("model", modelMat);
				m_GBufferShader->SetUniformMat4f("prevModel", modelMat);
				m_GBufferShader->SetUniformMat4f("viewProj", currentViewProj);
				m_GBufferShader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);

				mesh.gpuMesh->Draw();  // shader already bound by mat->Render()
				drewSomething = true;
			}

				float pixel[4] = {0};
				glReadBuffer(GL_COLOR_ATTACHMENT1);
		}

		if (!drewSomething)
		{
			m_GBufferShader->Bind();
			m_GBufferShader->SetUniformMat4f("viewProj", currentViewProj);
			m_GBufferShader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_DefaultTex);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, m_DefaultTex);

			mat4 model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
			mat4 mvp = proj * view * model;
			m_GBufferShader->SetUniformMat4f("MVP_matrix", mvp);
			m_GBufferShader->SetUniformMat4f("model", model);
			m_GBufferShader->SetUniformMat4f("prevModel", model);
			m_GBufferShader->SetUniform1i("hasNormalMap", 0);
			renderer.DrawElement(*va, *ibo, *m_GBufferShader);

				// Draw floor plane (flattened cube) to receive shadows
				mat4 floorModel = translate(mat4(1.0f), vec3(0.0f, -2.0f, 0.0f));
				floorModel = scale(floorModel, vec3(10.0f, 0.05f, 10.0f));
				m_GBufferShader->SetUniformMat4f("MVP_matrix", proj * view * floorModel);
				m_GBufferShader->SetUniformMat4f("model", floorModel);
				m_GBufferShader->SetUniformMat4f("prevModel", floorModel);
				renderer.DrawElement(*va, *ibo, *m_GBufferShader);
		}

		glEnable(GL_BLEND);
		m_GBuffer->UnBind();

		m_PrevViewProjMatrix = currentViewProj;
		m_FrameCount++;

		resources.GBufferPosition = m_GBuffer->GetPositionTexture();
		resources.GBufferNormal   = m_GBuffer->GetNormalTexture();
		resources.GBufferAlbedo   = m_GBuffer->GetAlbedoTexture();
		resources.GBufferSpecular = m_GBuffer->GetSpecularTexture();
		resources.VelocityTexture = m_GBuffer->GetVelocityTexture();
		resources.DepthTexture    = m_GBuffer->GetDepthTexture();
		resources.SourceFBO       = m_GBuffer->GetFBO();
	}

	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr)override {
		m_Spec.Width  = fb->GetSpecification().Width;
		m_Spec.Height = fb->GetSpecification().Height;
		m_GBuffer = CreatePtr<GBuffer>(m_Spec);
		m_GBufferShader = ShaderLibiray::Get("D:/Code/C++/Tsundere/res/shaders/GBuffer.shader");

		float position[] =
		{
			// ====== 前面 Front Face (z = -0.5), 法线 (0, 0, -1) ======
			-0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f, // 顶点 0 (左下)
			 0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f, // 顶点 1 (右下)
			 0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f, // 顶点 2 (右上)
			-0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f, // 顶点 3 (左上)

			// ====== 后面 Back Face (z = 0.5), 法线 (0, 0, 1) ======
			-0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 0.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f, // 顶点 4 (右下 - 从外看)
			 0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 0.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f, // 顶点 5 (左下 - 从外看)
			 0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f, // 顶点 6 (左上 - 从外看)
			-0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f, // 顶点 7 (右上 - 从外看)

			// ====== 左面 Left Face (x = -0.5), 法线 (-1, 0, 0) ======
			-0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f, // 顶点 8
			-0.5f, -0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f, // 顶点 9
			-0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 1.0f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f, // 顶点 10
			-0.5f,  0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f, // 顶点 11

			// ====== 右面 Right Face (x = 0.5), 法线 (1, 0, 0) ======
			 0.5f, -0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f, 1.0f, 0.0f, // 顶点 12
			 0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f, 1.0f, 0.0f, // 顶点 13
			 0.5f,  0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 1.0f,   0.0f, 0.0f,-1.0f,   0.0f, 1.0f, 0.0f, // 顶点 14
			 0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 1.0f,   0.0f, 0.0f,-1.0f,   0.0f, 1.0f, 0.0f, // 顶点 15

			 // ====== 顶面 Top Face (y = 0.5), 法线 (0, 1, 0) ======
			 -0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f, // 顶点 16
			  0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f, // 顶点 17
			  0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f, // 顶点 18
			 -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f, // 顶点 19

			 // ====== 底面 Bottom Face (y = -0.5), 法线 (0, -1, 0) ======
			 -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,  1.0f, // 顶点 20
			  0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,  1.0f, // 顶点 21
			  0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,  1.0f, // 22
			 -0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,  1.0f  // 23
		};

		unsigned int indices[] = {
			0,  1,  2,      2,  3,  0,  // 前面
			4,  5,  6,      6,  7,  4,  // 后面
			8,  9,  10,     10, 11, 8,  // 左面
			12, 13, 14,     14, 15, 12, // 右面
			16, 17, 18,     18, 19, 16, // 顶面
			20, 21, 22,     22, 23, 20  // 底面
		};

		va = CreatePtr<VertexArray>(24);
		vb = CreatePtr<VertexBuffer>(position, 24 * 14 * sizeof(float));
		ibo = CreatePtr<IndexBuffer>(indices, 36);
		VertexBufferLayout layout;
		layout.Push<float>(3);
		layout.Push<float>(3);
		layout.Push<float>(2);
		layout.Push<float>(3);
		layout.Push<float>(3);
		va->AddBuffer(*vb, layout);

		unsigned char white[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &m_DefaultTex);
		glBindTexture(GL_TEXTURE_2D, m_DefaultTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	void OnFboResize(unsigned int width, unsigned int height)
	{
		m_Spec.Width = width;
		m_Spec.Height = height;
		m_GBuffer->Resize(width, height);
	}

	// --- RenderGraph path ---------------------------------------------------
	// Declares this pass into a graph. The graph owns all six render targets;
	// the legacy m_GBuffer (its own FBO + textures) is untouched and keeps
	// serving Execute(), so both paths coexist behind the UI toggle.
	//
	// Formats mirror GBuffer.cpp exactly, with one deliberate substitution:
	// Position and Normal are GL_RGB16F there, but RHI's Format enum has no
	// RGB16F. RGBA16F has identical per-channel precision and the shader writes
	// vec3 / samples .rgb either way, so alpha is simply unused. Most drivers
	// pad RGB16F to RGBA16F internally regardless.
	struct GraphOutputs
	{
		RGTextureHandle Position;
		RGTextureHandle Normal;
		RGTextureHandle Albedo;
		RGTextureHandle Specular;
		RGTextureHandle Velocity;
		RGTextureHandle Depth;
	};

	GraphOutputs AddToGraph(
		RenderGraph& graph,
		Ref<Scene> scene,
		uint32_t width,
		uint32_t height)
	{
		RDGTextureDesc base;
		base.width  = width;
		base.height = height;
		base.usage  = TextureUsage::ColorAttachment | TextureUsage::Sampled;

		RDGTextureDesc positionDesc = base;
		positionDesc.format = Format::RGBA16F;

		RDGTextureDesc normalDesc = base;
		normalDesc.format = Format::RGBA16F;

		RDGTextureDesc albedoDesc = base;
		albedoDesc.format = Format::RGBA8_UNORM;

		RDGTextureDesc specularDesc = base;
		specularDesc.format = Format::RGBA16F;

		RDGTextureDesc velocityDesc = base;
		velocityDesc.format = Format::RG16F;

		RDGTextureDesc depthDesc;
		depthDesc.width  = width;
		depthDesc.height = height;
		depthDesc.format = Format::D24_UNORM_S8_UINT;
		depthDesc.usage  = TextureUsage::DepthStencil | TextureUsage::Sampled;

		GraphOutputs out;
		out.Position = graph.CreateTexture(positionDesc, "GBuffer.Position");
		out.Normal   = graph.CreateTexture(normalDesc,   "GBuffer.Normal");
		out.Albedo   = graph.CreateTexture(albedoDesc,   "GBuffer.Albedo");
		out.Specular = graph.CreateTexture(specularDesc, "GBuffer.Specular");
		out.Velocity = graph.CreateTexture(velocityDesc, "GBuffer.Velocity");
		out.Depth    = graph.CreateTexture(depthDesc,    "GBuffer.Depth");

		graph.AddPass(
			"GBuffer",
			[out](RenderGraphPassBuilder& builder)
			{
				// All five MRT slots clear to (0,0,0,0), matching the legacy
				// glClearColor(0,0,0,0). DeferredLighting relies on a zero
				// Position to tell sky from geometry, so this must not change.
				const std::array<float, 4> zero = { 0.0f, 0.0f, 0.0f, 0.0f };

				builder.SetColorAttachment(0, out.Position, RGLoadOp::Clear, zero);
				builder.SetColorAttachment(1, out.Normal,   RGLoadOp::Clear, zero);
				builder.SetColorAttachment(2, out.Albedo,   RGLoadOp::Clear, zero);
				builder.SetColorAttachment(3, out.Specular, RGLoadOp::Clear, zero);
				builder.SetColorAttachment(4, out.Velocity, RGLoadOp::Clear, zero);
				builder.SetDepthAttachment(out.Depth, RGLoadOp::Clear, 1.0f);
			},
			[this, scene, width, height](
				RHICommandBuffer& cmd, RenderGraphResources& resources)
			{
				mat4 view = currentcamera->GetViewFront();
				mat4 proj = currentcamera->GetProj();
				mat4 currentViewProj = proj * view;

				if (EnableJitter)
					proj = Jittering(proj, (float)width, (float)height);

				// The graph has bound the FBO, set the viewport and cleared all
				// six attachments. Only per-pass GL state remains.
				Renderer renderer;
				glDisable(GL_BLEND);
				glEnable(GL_DEPTH_TEST);
				glDepthFunc(GL_LESS);
				glEnable(GL_CULL_FACE);
				glCullFace(GL_BACK);

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, m_DefaultTex);

				m_GBufferShader->Bind();
				bool drewSomething = false;

				for (auto [entityID, transform, meshrender]
					 : scene->m_Registry.view<Component::Transform, Component::MeshRender>().each())
				{
					if (meshrender.ModelPath.empty() || meshrender.materials.empty())
						continue;

					Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
					if (!model || model->meshes.empty())
						continue;

					mat4 modelMat = transform.GetTransform();

					for (size_t i = 0; i < model->meshes.size(); i++)
					{
						auto& mesh = model->meshes[i];
						if (!mesh.IsGPUReady())
							continue;

						Ref<Material> mat = i < meshrender.materials.size()
							? meshrender.materials[i]
							: meshrender.materials[0];

						mat->Render(m_GBufferShader);

						m_GBufferShader->SetUniformMat4f("MVP_matrix", proj * view * modelMat);
						m_GBufferShader->SetUniformMat4f("model", modelMat);
						m_GBufferShader->SetUniformMat4f("prevModel", modelMat);
						m_GBufferShader->SetUniformMat4f("viewProj", currentViewProj);
						m_GBufferShader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);

						mesh.gpuMesh->Draw();  // shader already bound by mat->Render()
						drewSomething = true;
					}
				}

				if (!drewSomething)
				{
					m_GBufferShader->Bind();
					m_GBufferShader->SetUniformMat4f("viewProj", currentViewProj);
					m_GBufferShader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);

					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, m_DefaultTex);
					glActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_2D, m_DefaultTex);

					mat4 model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
					mat4 mvp = proj * view * model;
					m_GBufferShader->SetUniformMat4f("MVP_matrix", mvp);
					m_GBufferShader->SetUniformMat4f("model", model);
					m_GBufferShader->SetUniformMat4f("prevModel", model);
					m_GBufferShader->SetUniform1i("hasNormalMap", 0);
					renderer.DrawElement(*va, *ibo, *m_GBufferShader);

					// Floor plane (flattened cube) to receive shadows
					mat4 floorModel = translate(mat4(1.0f), vec3(0.0f, -2.0f, 0.0f));
					floorModel = scale(floorModel, vec3(10.0f, 0.05f, 10.0f));
					m_GBufferShader->SetUniformMat4f("MVP_matrix", proj * view * floorModel);
					m_GBufferShader->SetUniformMat4f("model", floorModel);
					m_GBufferShader->SetUniformMat4f("prevModel", floorModel);
					renderer.DrawElement(*va, *ibo, *m_GBufferShader);
				}

				glEnable(GL_BLEND);

				m_PrevViewProjMatrix = currentViewProj;
				m_FrameCount++;
			});

		return out;
	}

	mat4 Jittering(const mat4& originalProj, float width, float height)
	{
		int jitterIndex = m_FrameCount % 16;
		vec2 currentJitter = GetHaltonJitter(jitterIndex);
		float deltaX = currentJitter.x * 2.0f / width;
		float deltaY = currentJitter.y * 2.0f / height;
		mat4 jitteredProjMatrix = originalProj;
		jitteredProjMatrix[2][0] += deltaX;
		jitteredProjMatrix[2][1] += deltaY;
		return jitteredProjMatrix;
	}
	vec2 GetHaltonJitter(int index) {
		auto halton = [](int index, int base) -> float {
			float f = 1.0f;
			float r = 0.0f;
			int current = index;
			while (current > 0) {
				f = f / base;
				r = r + f * (current % base);
				current = current / base;
			}
			return r;
		};
		return vec2(halton(index + 1, 2) - 0.5f, halton(index + 1, 3) - 0.5f);
	}
};





