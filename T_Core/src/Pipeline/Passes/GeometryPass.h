#pragma once
#include "Pipeline/Passes/PassCommon.h"

class  GeometryPass : public RenderPass
{
	Ref<RHIFramebuffer>m_RHIGbuffer;

	int m_FrameCount = 0;

	Ref<RHITexture2D>RHI_DefaultTex;

	mat4 m_PrevViewProjMatrix = mat4(1.0f);

	Ref<RHIBuffer> m_CubeVB;           // fallback cube VB (RHI)
	Ref<RHIBuffer> m_CubeIB;           // fallback cube IB (RHI)
	Ref<RHIPipeline> m_CubePipeline;   // fallback cube pipeline (VAO + shader + state)
	Ref<RHIShader>m_LitShader;   // Lit.shader as RHI — writes to location 0+1 (color+velocity)


public:
	bool EnableJitter = true;

	void Execute(Ref<Scene> scene, RenderResources& resources) override {
		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 currentViewProj = proj * view;

		if (EnableJitter)
			proj = Jittering(proj, m_RHIGbuffer->GetWidth(), m_RHIGbuffer->GetHeight());

		vec3 lightDir = vec3(-0.5f, -1.0f, -0.5f);
		vec3 lightColor = vec3(1.0f);
		float ambientStrength = 0.1f;
		vec3 viewPos = currentcamera->getpos();
		for (auto entityID : scene->m_Registry.view<Component::DirectionalLight>())
		{
			auto& dl = scene->m_Registry.get<Component::DirectionalLight>(entityID);
			lightDir = dl.Direction;
			lightColor = dl.Color * dl.Intensity;
			ambientStrength = dl.Ambient;
			break;
		}

		Renderer renderer;
		bool drewSomething = false;
		RHI_DefaultTex->Bind(0);

		// Shadow Map — Bind shader FIRST so uniforms go to the correct program
		m_LitShader->Bind();
		glActiveTexture(GL_TEXTURE5);
		if (resources.ShadowMapDepth)
		{
			glBindTexture(GL_TEXTURE_2D, resources.ShadowMapDepth);
			m_LitShader->SetUniform1i("u_ShadowMapEnabled", 1);
			m_LitShader->SetUniformMat4f("u_LightViewProj", resources.ShadowLightViewProj);
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, 0);
			m_LitShader->SetUniform1i("u_ShadowMapEnabled", 0);
			m_LitShader->SetUniformMat4f("u_LightViewProj", glm::mat4(1.0f));
		}
		m_LitShader->SetUniform1i("u_ShadowMap", 5);
		m_LitShader->SetUniformVec2("u_ShadowMapSize", glm::vec2(2048.0f, 2048.0f));
		m_LitShader->SetUniform1f("u_LightSize", 50.0f);

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

				mat->Render(m_LitShader);

				// Material::Render() re-uploads every uniform it found in the
				// shader source, and Lit.shader declares u_ShadowMap as a
				// sampler2D — so the material claims it as its 4th texture and
				// overwrites u_ShadowMap with slot 4 (an empty material texture).
				// u_ShadowMapSize / u_LightSize get zeroed the same way. The
				// shadow map itself is still bound to GL_TEXTURE5; only the
				// uniforms were clobbered, so restore them.
				m_LitShader->SetUniform1i("u_ShadowMap", 5);
				m_LitShader->SetUniform1i("u_ShadowMapEnabled", resources.ShadowMapDepth ? 1 : 0);
				m_LitShader->SetUniformVec2("u_ShadowMapSize", glm::vec2(2048.0f, 2048.0f));
				m_LitShader->SetUniform1f("u_LightSize", 50.0f);

				m_LitShader->SetUniformMat4f("MVP_matrix", proj * view * modelMat);
				m_LitShader->SetUniformMat4f("model", modelMat);
				m_LitShader->SetUniformMat4f("prevModel", modelMat);
				m_LitShader->SetUniformMat4f("viewProj", currentViewProj);
				m_LitShader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);
				m_LitShader->SetUniformVec3("lightDir", lightDir);
				m_LitShader->SetUniformVec3("lightColor", lightColor);
				m_LitShader->SetUniform1f("ambientStrength", ambientStrength);
				m_LitShader->SetUniformVec3("viewPos", viewPos);
				m_LitShader->SetUniform1i("hasNormalMap", 0);

				mesh.gpuMesh->Draw();  // shader already bound by mat->Render()
				drewSomething = true;
			}
		}

		if (!drewSomething)
		{
			m_LitShader->Bind();
			m_LitShader->SetUniformMat4f("viewProj", currentViewProj);
			m_LitShader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);
			m_LitShader->SetUniformVec3("lightDir", lightDir);
			m_LitShader->SetUniformVec3("lightColor", lightColor);
			m_LitShader->SetUniform1f("ambientStrength", ambientStrength);
			m_LitShader->SetUniformVec3("viewPos", viewPos);
			m_LitShader->SetUniform1i("hasNormalMap", 0);

			{
				auto cmd = RHIRenderer::GetCmd();

				// Draw cube (pipeline binds shader — set MVP first)
				mat4 model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
				mat4 mvp = proj * view * model;
				m_LitShader->SetUniformMat4f("MVP_matrix", mvp);
				m_LitShader->SetUniformMat4f("model", model);
				m_LitShader->SetUniformMat4f("prevModel", model);
				cmd->BindPipeline(m_CubePipeline);
				cmd->DrawIndexed(36);  // cube

				// Draw floor plane (pipeline still bound — set floor MVP now)
				mat4 floorModel = translate(mat4(1.0f), vec3(0.0f, -2.0f, 0.0f));
				floorModel = scale(floorModel, vec3(10.0f, 0.05f, 10.0f));
				mat4 floorMVP = proj * view * floorModel;
				m_LitShader->SetUniformMat4f("MVP_matrix", floorMVP);
				m_LitShader->SetUniformMat4f("model", floorModel);
				m_LitShader->SetUniformMat4f("prevModel", floorModel);
				cmd->DrawIndexed(36);  // floor

				m_CubePipeline->Unbind();
			}
		}

		m_PrevViewProjMatrix = currentViewProj;
		m_FrameCount++;
		resources.SceneColorTexture = m_RHIGbuffer->GetColorAttachmentID(0);
		resources.VelocityTexture = m_RHIGbuffer->GetColorAttachmentID(1);
		resources.DepthTexture = m_RHIGbuffer->GetDepthAttachmentID();
	}

	// --- RenderGraph path ---------------------------------------------------
	// Same draw work as Execute(), but the render targets are graph resources
	// and the graph binds/clears them. Returns the three textures produced.
	//
	// shadowDepth may be a default (invalid) handle, meaning "no shadow map this
	// frame" — the pass then binds a null shadow texture like Execute() does.
	struct GraphOutputs
	{
		RGTextureHandle SceneColor;
		RGTextureHandle Velocity;
		RGTextureHandle Depth;
	};

	GraphOutputs AddToGraph(
		RenderGraph& graph,
		Ref<Scene> scene,
		Ref<RGFrameData> frameData,
		RGTextureHandle shadowDepth,
		uint32_t width,
		uint32_t height)
	{
		RDGTextureDesc colorDesc;
		colorDesc.width  = width;
		colorDesc.height = height;
		colorDesc.format = Format::RGBA8_UNORM;
		colorDesc.usage  = TextureUsage::ColorAttachment | TextureUsage::Sampled;

		RDGTextureDesc velocityDesc = colorDesc;
		velocityDesc.format = Format::RG16F;

		RDGTextureDesc depthDesc;
		depthDesc.width  = width;
		depthDesc.height = height;
		depthDesc.format = Format::D24_UNORM_S8_UINT;
		depthDesc.usage  = TextureUsage::DepthStencil | TextureUsage::Sampled;

		GraphOutputs out;
		out.SceneColor = graph.CreateTexture(colorDesc, "Geometry.SceneColor");
		out.Velocity   = graph.CreateTexture(velocityDesc, "Geometry.Velocity");
		out.Depth      = graph.CreateTexture(depthDesc, "Geometry.Depth");

		const bool hasShadow = (shadowDepth.id != InvalidResourceId);

		graph.AddPass(
			"Geometry",
			[out, shadowDepth, hasShadow](RenderGraphPassBuilder& builder)
			{
				if (hasShadow)
					builder.ReadTexture(shadowDepth, RGAccess::ReadSRV);

				// MRT: Lit.shader writes FragColor to slot 0, MotionVector to 1.
				builder.SetColorAttachment(0, out.SceneColor, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 1.0f });
				builder.SetColorAttachment(1, out.Velocity,   RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 0.0f });
				builder.SetDepthAttachment(out.Depth, RGLoadOp::Clear, 1.0f);
			},
			[this, scene, frameData, shadowDepth, hasShadow, width, height](RHICommandBuffer& cmd, RenderGraphResources& resources)
			{
				mat4 view = currentcamera->GetViewFront();
				mat4 proj = currentcamera->GetProj();
				mat4 currentViewProj = proj * view;

				if (EnableJitter)
					proj = Jittering(proj, (float)width, (float)height);

				vec3 lightDir = vec3(-0.5f, -1.0f, -0.5f);
				vec3 lightColor = vec3(1.0f);
				float ambientStrength = 0.1f;
				vec3 viewPos = currentcamera->getpos();
				for (auto entityID : scene->m_Registry.view<Component::DirectionalLight>())
				{
					auto& dl = scene->m_Registry.get<Component::DirectionalLight>(entityID);
					lightDir = dl.Direction;
					lightColor = dl.Color * dl.Intensity;
					ambientStrength = dl.Ambient;
					break;
				}

				// Depth testing is not enabled globally in the app init path, and
				// the mesh draw path below does not bind a pipeline that would set
				// it. Enable it explicitly so graph geometry depth-sorts correctly.
				glEnable(GL_DEPTH_TEST);
				glDepthFunc(GL_LESS);

				// Skybox first, matching the legacy forward chain, which drew it
				// into the bound FBO before the geometry pass ran. It masks depth
				// writes internally and restores them, leaving the state above intact.
				currentcamera->RenderSkyBox();

				Renderer renderer;
				bool drewSomething = false;
				RHI_DefaultTex->Bind(0);

				// Bind shader before setting uniforms so they reach this program.
				m_LitShader->Bind();
				glActiveTexture(GL_TEXTURE5);
				RHITexture2D* shadowTex = hasShadow ? resources.GetTexture(shadowDepth) : nullptr;
				if (shadowTex)
				{
					glBindTexture(GL_TEXTURE_2D, (GLuint)shadowTex->GetNativeID());
					m_LitShader->SetUniform1i("u_ShadowMapEnabled", 1);
					m_LitShader->SetUniformMat4f("u_LightViewProj", frameData->ShadowLightViewProj);
				}
				else
				{
					glBindTexture(GL_TEXTURE_2D, 0);
					m_LitShader->SetUniform1i("u_ShadowMapEnabled", 0);
					m_LitShader->SetUniformMat4f("u_LightViewProj", glm::mat4(1.0f));
				}
				m_LitShader->SetUniform1i("u_ShadowMap", 5);
				m_LitShader->SetUniformVec2("u_ShadowMapSize", glm::vec2(2048.0f, 2048.0f));
				m_LitShader->SetUniform1f("u_LightSize", 50.0f);

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

						mat->Render(m_LitShader);

						// See the same restore in Execute(): Material::Render()
						// claims u_ShadowMap as one of its own textures and
						// zeroes u_ShadowMapSize / u_LightSize.
						m_LitShader->SetUniform1i("u_ShadowMap", 5);
						m_LitShader->SetUniform1i("u_ShadowMapEnabled", shadowTex ? 1 : 0);
						m_LitShader->SetUniformVec2("u_ShadowMapSize", glm::vec2(2048.0f, 2048.0f));
						m_LitShader->SetUniform1f("u_LightSize", 50.0f);

						m_LitShader->SetUniformMat4f("MVP_matrix", proj * view * modelMat);
						m_LitShader->SetUniformMat4f("model", modelMat);
						m_LitShader->SetUniformMat4f("prevModel", modelMat);
						m_LitShader->SetUniformMat4f("viewProj", currentViewProj);
						m_LitShader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);
						m_LitShader->SetUniformVec3("lightDir", lightDir);
						m_LitShader->SetUniformVec3("lightColor", lightColor);
						m_LitShader->SetUniform1f("ambientStrength", ambientStrength);
						m_LitShader->SetUniformVec3("viewPos", viewPos);
						m_LitShader->SetUniform1i("hasNormalMap", 0);

						mesh.gpuMesh->Draw();
						drewSomething = true;
					}
				}

				if (!drewSomething)
				{
					m_LitShader->Bind();
					m_LitShader->SetUniformMat4f("viewProj", currentViewProj);
					m_LitShader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);
					m_LitShader->SetUniformVec3("lightDir", lightDir);
					m_LitShader->SetUniformVec3("lightColor", lightColor);
					m_LitShader->SetUniform1f("ambientStrength", ambientStrength);
					m_LitShader->SetUniformVec3("viewPos", viewPos);
					m_LitShader->SetUniform1i("hasNormalMap", 0);

					mat4 model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
					mat4 mvp = proj * view * model;
					m_LitShader->SetUniformMat4f("MVP_matrix", mvp);
					m_LitShader->SetUniformMat4f("model", model);
					m_LitShader->SetUniformMat4f("prevModel", model);
					cmd.BindPipeline(m_CubePipeline);
					cmd.DrawIndexed(36);

					mat4 floorModel = translate(mat4(1.0f), vec3(0.0f, -2.0f, 0.0f));
					floorModel = scale(floorModel, vec3(10.0f, 0.05f, 10.0f));
					mat4 floorMVP = proj * view * floorModel;
					m_LitShader->SetUniformMat4f("MVP_matrix", floorMVP);
					m_LitShader->SetUniformMat4f("model", floorModel);
					m_LitShader->SetUniformMat4f("prevModel", floorModel);
					cmd.DrawIndexed(36);

					m_CubePipeline->Unbind();
				}

				m_PrevViewProjMatrix = currentViewProj;
				m_FrameCount++;
			});

		return out;
	}

	void Init(Ref<FrameBuffer>& m_GBuffer, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr)override {
		this->m_RHIGbuffer = RHIFrameBuffer;


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
			0,  2,  1,      3,  2,  0,  // 前面
			4,  5,  6,      6,  7,  4,  // 后面
			8,  9,  10,     10, 11, 8,  // 左面
			12, 13, 14,     14, 15, 12, // 右面
			16, 17, 18,     18, 19, 16, // 顶面
			20, 21, 22,     22, 23, 20  // 底面
		};

		m_CubeVB = RHIBuffer::Create({ 24 * 14 * (uint32_t)sizeof(float), BufferUsage::Vertex, false, position });
		m_CubeIB = RHIBuffer::Create({ 36 * (uint32_t)sizeof(unsigned int), BufferUsage::Index, false, indices });

		m_LitShader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/Lit.shader");

		// Build pipeline with the same vertex layout as the old VAO
		{
			VertexLayout vtxLayout;
			vtxLayout.stride = 14 * sizeof(float);
			vtxLayout.attributes = {
				{ 0, VertexFormat::Float3, 0, 0 },
				{ 1, VertexFormat::Float3, 3 * sizeof(float), 0 },
				{ 2, VertexFormat::Float2, 6 * sizeof(float), 0 },
				{ 3, VertexFormat::Float3, 8 * sizeof(float), 0 },
				{ 4, VertexFormat::Float3, 11 * sizeof(float), 0 },
			};

			PipelineDesc pipeDesc;
			pipeDesc.shader       = m_LitShader;
			pipeDesc.vertexLayout = vtxLayout;
			pipeDesc.topology     = PrimitiveTopology::Triangles;
			pipeDesc.cullMode     = CullMode::Back;     // cull back faces, show front faces
			pipeDesc.depthTest    = true;
			pipeDesc.depthWrite   = true;
			pipeDesc.srcBlend     = BlendFactor::One;    // no blend for geometry pass
			pipeDesc.dstBlend     = BlendFactor::Zero;

			m_CubePipeline = RHIPipeline::Create(pipeDesc);

			// Bake VB format + IB into the pipeline (GL: VAO setup; VK: no-op).
			// This is a one-time setup; subsequent BindPipeline() restores the full VAO state.
			m_CubePipeline->SetupVertexFormat(m_CubeVB);
			m_CubePipeline->SetupIndexBuffer(m_CubeIB);
		}
		unsigned char white[4] = { 255, 255, 255, 255 };
		RHI_DefaultTex = RHITexture2D::Create({ 1,1,Format::RGBA8_UNORM ,FilterMode::Linear,FilterMode::Linear,WrapMode::ClampToEdge ,WrapMode::ClampToEdge, false,white });

		}

	void OnFboResize(unsigned int width, unsigned int height)
	{
		m_RHIGbuffer->Resize(width,height);
	}

	mat4 Jittering(const mat4& originalProj, float width, float height)
		{
		// ���� 16 ��λ�� Halton ����
		int jitterIndex = m_FrameCount % 16;
		vec2 currentJitter = GetHaltonJitter(jitterIndex);

		// ת��Ϊ NDC �ռ�ƫ�� (-1 �� 1 �Ŀռ䣬���Գ��� 2.0 / �ֱ���)
		float deltaX = currentJitter.x * 2.0f / width;
		float deltaY = currentJitter.y * 2.0f / height;

		mat4 jitteredProjMatrix = originalProj;
		jitteredProjMatrix[2][0] += deltaX; // OpenGL �����������޸ĵ����е�һ��
		jitteredProjMatrix[2][1] += deltaY; // �޸ĵ����еڶ���

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

		// ����ӳ�䵽 [-0.5, 0.5] ��ƫ��
		return vec2(halton(index + 1, 2) - 0.5f, halton(index + 1, 3) - 0.5f);
	}
};


