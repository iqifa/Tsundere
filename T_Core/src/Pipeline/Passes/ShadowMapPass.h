#pragma once
#include "Pipeline/Passes/PassCommon.h"

// =============================================================================
// ShadowMapPass — Renders scene depth from the directional light's perspective
// into a depth-only FBO (GL_DEPTH_COMPONENT32F). Follows the same manual-GL
// pattern as the existing ShadowPass to avoid touching the in-progress RHI
// migration.
// =============================================================================
class ShadowMapPass : public RenderPass
{
public:
	int  ShadowMapWidth  = 2048;
	int  ShadowMapHeight = 2048;
	bool Enabled         = true;

	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();
		m_DepthShader = CreateRef<GLShader>("D:/Code/C++/Tsundere/res/shaders/ShadowMapDepth.shader");
		CreateShadowMap(ShadowMapWidth, ShadowMapHeight);
		CreateFallbackCube();
	}

	void Execute(Ref<Scene> scene, RenderResources& resources) override
	{
		if (!Enabled)
			return;

		// --- Step 1: compute light view-projection matrix ---
		mat4 cameraView = currentcamera->GetViewFront();
		mat4 cameraProj = currentcamera->GetProj();
		mat4 lightView, lightProj;
		ComputeLightViewProj(scene, cameraView, cameraProj, lightView, lightProj);
		mat4 lightViewProj = lightProj * lightView;

		// --- Step 2: render scene geometry into depth-only FBO ---
		glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowMapFBO);
		glViewport(0, 0, ShadowMapWidth, ShadowMapHeight);
		glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);  // standard culling — store closest surface depth
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // no color writes

		m_DepthShader->Bind();
		m_DepthShader->SetUniformMat4f("u_LightViewProj", lightViewProj);

		Renderer renderer;
		bool drewSomething = false;

		for (auto [entityID, transform, meshrender]
			 : scene->m_Registry.view<Component::Transform, Component::MeshRender>().each())
		{
			if (meshrender.ModelPath.empty())
				continue;

			Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
			if (!model)
				continue;

			mat4 modelMat = transform.GetTransform();
			m_DepthShader->SetUniformMat4f("u_Model", modelMat);

			for (auto& mesh : model->meshes)
			{
				if (!mesh.IsGPUReady())
					continue;
				mesh.gpuMesh->Draw();  // m_DepthShader->Bind() called before loop
				drewSomething = true;
			}
		}

		// Fallback cube when no scene geometry is loaded
		if (!drewSomething)
		{
			// Draw cube
			mat4 cubeModel = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
			m_DepthShader->SetUniformMat4f("u_Model", cubeModel);
			renderer.DrawElement(*m_FallbackVA, *m_FallbackIB, *m_DepthShader);

			// Draw floor plane (flattened cube) to receive shadows
			mat4 floorModel = translate(mat4(1.0f), vec3(0.0f, -2.0f, 0.0f));
			floorModel = scale(floorModel, vec3(10.0f, 0.05f, 10.0f));
			m_DepthShader->SetUniformMat4f("u_Model", floorModel);
			renderer.DrawElement(*m_FallbackVA, *m_FallbackIB, *m_DepthShader);
		}

		// Restore GL state
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glCullFace(GL_BACK);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// --- Step 3: store output in resources ---
		resources.ShadowMapDepth     = m_ShadowMapDepth;
		resources.ShadowLightViewProj = lightViewProj;
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width  = w;
		m_Spec.Height = h;
		// Shadow map resolution is independent of viewport size.
	}

	// --- RenderGraph path ---------------------------------------------------
	// Declares this pass into a graph and returns the depth texture it produces.
	// The graph owns the texture and the FBO; only the draw work stays here.
	// Execute() above is untouched so the legacy chain keeps working.
	RGTextureHandle AddToGraph(RenderGraph& graph, Ref<Scene> scene, Ref<RGFrameData> frameData)
	{
		RDGTextureDesc desc;
		desc.width      = (uint32_t)ShadowMapWidth;
		desc.height     = (uint32_t)ShadowMapHeight;
		desc.format     = Format::D32_SFLOAT;
		desc.usage      = TextureUsage::DepthStencil | TextureUsage::Sampled;
		// ClampToBorder + white border: a lookup outside the light frustum must
		// read depth 1.0, i.e. "not in shadow", rather than smearing the edge.
		desc.wrapS       = WrapMode::ClampToBorder;
		desc.wrapT       = WrapMode::ClampToBorder;
		desc.borderColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		RGTextureHandle shadowDepth = graph.CreateTexture(desc, "ShadowMap.Depth");

		graph.AddPass(
			"ShadowMap",
			[shadowDepth](RenderGraphPassBuilder& builder)
			{
				// Depth-only: no color attachment at all.
				builder.SetDepthAttachment(shadowDepth, RGLoadOp::Clear, 1.0f);
			},
			[this, scene, frameData, shadowDepth](RHICommandBuffer& cmd, RenderGraphResources& resources)
			{
				mat4 cameraView = currentcamera->GetViewFront();
				mat4 cameraProj = currentcamera->GetProj();
				mat4 lightView, lightProj;
				ComputeLightViewProj(scene, cameraView, cameraProj, lightView, lightProj);
				mat4 lightViewProj = lightProj * lightView;

				// The graph has already bound the FBO, set the viewport and
				// cleared depth. Only per-pass GL state is left to us.
				glEnable(GL_DEPTH_TEST);
				glDepthFunc(GL_LESS);
				glEnable(GL_CULL_FACE);
				glCullFace(GL_BACK);
				// No glColorMask needed: the depth-only FBO declares GL_NONE as
				// its draw buffer, so fragment color output goes nowhere anyway.

				m_DepthShader->Bind();
				m_DepthShader->SetUniformMat4f("u_LightViewProj", lightViewProj);

				Renderer renderer;
				bool drewSomething = false;

				for (auto [entityID, transform, meshrender]
					 : scene->m_Registry.view<Component::Transform, Component::MeshRender>().each())
				{
					if (meshrender.ModelPath.empty())
						continue;

					Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
					if (!model)
						continue;

					m_DepthShader->SetUniformMat4f("u_Model", transform.GetTransform());

					for (auto& mesh : model->meshes)
					{
						if (!mesh.IsGPUReady())
							continue;
						mesh.gpuMesh->Draw();
						drewSomething = true;
					}
				}

				if (!drewSomething)
				{
					mat4 cubeModel = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
					m_DepthShader->SetUniformMat4f("u_Model", cubeModel);
					renderer.DrawElement(*m_FallbackVA, *m_FallbackIB, *m_DepthShader);

					mat4 floorModel = translate(mat4(1.0f), vec3(0.0f, -2.0f, 0.0f));
					floorModel = scale(floorModel, vec3(10.0f, 0.05f, 10.0f));
					m_DepthShader->SetUniformMat4f("u_Model", floorModel);
					renderer.DrawElement(*m_FallbackVA, *m_FallbackIB, *m_DepthShader);
				}

				// Publish the CPU-side result for GeometryPass.
				frameData->ShadowLightViewProj = lightViewProj;
				frameData->HasShadowMap        = true;

				// Native ID for the ImGui debug view, which reads it after the
				// graph has finished running.
				RHITexture2D* depthTex = resources.GetTexture(shadowDepth);
				frameData->ShadowMapDepthID = depthTex
					? static_cast<unsigned int>(depthTex->GetNativeID())
					: 0;
			});

		return shadowDepth;
	}

private:
	// -------------------------------------------------------------------------
	// Create the depth-only FBO + D32_SFLOAT texture
	// -------------------------------------------------------------------------
	void CreateShadowMap(unsigned int w, unsigned int h)
	{
		if (m_ShadowMapDepth) glDeleteTextures(1, &m_ShadowMapDepth);
		if (m_ShadowMapFBO)  glDeleteFramebuffers(1, &m_ShadowMapFBO);

		// Depth texture
		glGenTextures(1, &m_ShadowMapDepth);
		glBindTexture(GL_TEXTURE_2D, m_ShadowMapDepth);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
		             w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
		glBindTexture(GL_TEXTURE_2D, 0);

		// Depth-only FBO (no color attachments)
		glGenFramebuffers(1, &m_ShadowMapFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowMapFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		                       GL_TEXTURE_2D, m_ShadowMapDepth, 0);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			Error_Core("ShadowMapPass: depth-only FBO is not complete!");

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	// -------------------------------------------------------------------------
	// Fallback cube (same 14-float/vert data as GeometryPass::Init lines 177-221)
	// -------------------------------------------------------------------------
	void CreateFallbackCube()
	{
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

		m_FallbackVA = CreatePtr<VertexArray>(36);
		m_FallbackVB = CreatePtr<VertexBuffer>(position, 36 * 14 * sizeof(float));
		m_FallbackIB = CreatePtr<IndexBuffer>(indices, 36);

		VertexBufferLayout layout;
		layout.Push<float>(3);  // loc 0: position
		layout.Push<float>(3);  // loc 1: normal
		layout.Push<float>(2);  // loc 2: texcoord
		layout.Push<float>(3);  // loc 3: tangent
		layout.Push<float>(3);  // loc 4: bitangent
		m_FallbackVA->AddBuffer(*m_FallbackVB, layout);
	}

	// -------------------------------------------------------------------------
	// Compute light view-projection matrix for directional light
	// -------------------------------------------------------------------------
	void ComputeLightViewProj(Ref<Scene> scene,
	                          const mat4& cameraView, const mat4& cameraProj,
	                          mat4& outLightView, mat4& outLightProj)
	{
		// 1. Extract light direction from scene
		vec3 lightDir = normalize(vec3(-0.5f, -1.0f, -0.5f));
		for (auto entityID : scene->m_Registry.view<Component::DirectionalLight>())
		{
			lightDir = normalize(scene->m_Registry.get<Component::DirectionalLight>(entityID).Direction);
			break;
		}

		// 2. Compute camera frustum 8 corners in world space
		mat4 invVP = inverse(cameraProj * cameraView);
		vec3 corners[8];
		for (int z = 0; z < 2; z++)
		{
			for (int y = 0; y < 2; y++)
			{
				for (int x = 0; x < 2; x++)
				{
					vec4 ndc = vec4(x * 2.0f - 1.0f, y * 2.0f - 1.0f, z * 2.0f - 1.0f, 1.0f);
					vec4 ws = invVP * ndc;
					corners[z * 4 + y * 2 + x] = vec3(ws) / ws.w;
				}
			}
		}

		// 3. Light view matrix: look from behind camera along light direction
		vec3 cameraPos = currentcamera->getpos();
		vec3 lightTarget = cameraPos;
		vec3 lightPos = lightTarget - lightDir * 50.0f;
		vec3 up = vec3(0.0f, 1.0f, 0.0f);
		if (abs(dot(lightDir, up)) > 0.99f)
			up = vec3(1.0f, 0.0f, 0.0f);
		outLightView = glm::lookAt(lightPos, lightTarget, up);

		// 4. Transform frustum corners to light space, compute AABB
		vec3 minLS = vec3( 3.4e38f);
		vec3 maxLS = vec3(-3.4e38f);
		for (int i = 0; i < 8; i++)
		{
			vec3 ls = vec3(outLightView * vec4(corners[i], 1.0f));
			minLS = min(minLS, ls);
			maxLS = max(maxLS, ls);
		}

		// 5. Texel-snapping: round AABB to shadow-map texel boundaries.
		// This prevents shadow edges from "swimming" when the camera moves
		// — the shadow map texels stay at fixed world-space positions.
		{
			float texelSizeX = (maxLS.x - minLS.x) / (float)ShadowMapWidth;
			float texelSizeY = (maxLS.y - minLS.y) / (float)ShadowMapHeight;
			float texelSize = std::max(texelSizeX, texelSizeY);

			if (texelSize > 0.0f)
			{
				minLS.x = std::floor(minLS.x / texelSize) * texelSize;
				minLS.y = std::floor(minLS.y / texelSize) * texelSize;
				maxLS.x = std::ceil (maxLS.x / texelSize) * texelSize;
				maxLS.y = std::ceil (maxLS.y / texelSize) * texelSize;
			}
		}

		// 6. Orthographic projection from AABB with padding
		float nearZ = -maxLS.z - 50.0f;
		float farZ  = -minLS.z + 50.0f;
		if (nearZ <= 0.0f) nearZ = 0.1f;
		outLightProj = glm::ortho(minLS.x, maxLS.x, minLS.y, maxLS.y, nearZ, farZ);
	}

	FrameBufferSpecification m_Spec;
	Ref<GLShader>      m_DepthShader;
	unsigned int     m_ShadowMapFBO   = 0;
	unsigned int     m_ShadowMapDepth = 0;
	Ptr<VertexArray> m_FallbackVA;
	Ptr<VertexBuffer>m_FallbackVB;
	Ptr<IndexBuffer> m_FallbackIB;
};

