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

	// PerDraw_Geometry UBO: 5 mat4 = 320 bytes, uploaded every draw.
	struct GeometryDrawUBO
	{
		glm::mat4 MVP_matrix;       //   0
		glm::mat4 model;            //  64
		glm::mat4 prevModel;        // 128
		glm::mat4 viewProj;         // 192
		glm::mat4 prevViewProj;     // 256
	};
	// PerFrame_Geometry UBO: vec4(16)+vec4(16)+float(4)+12pad+vec4(16)+mat4(64)+int(4)+int(4)+vec2(8)+float(4)+12pad = 160 bytes
	struct GeometryFrameUBO
	{
		glm::vec4 lightDir;             //   0  (vec3 → vec4)
		glm::vec4 lightColor;           //  16  (vec3 → vec4)
		float     ambientStrength;      //  32
		float     _pad0[3];             //  36..47 — std140 vec4 align
		glm::vec4 viewPos;              //  48  (vec3 → vec4)
		glm::mat4 u_LightViewProj;      //  64
		int32_t   hasNormalMap;         // 128
		int32_t   u_ShadowMapEnabled;   // 132
		glm::vec2 u_ShadowMapSize;      // 136
		float     u_LightSize;          // 144
		int32_t   _pad1[3];             // 148..159 — std140 vec4 align
	};
	static_assert(sizeof(GeometryDrawUBO)  == 320, "GeometryDrawUBO");
	static_assert(sizeof(GeometryFrameUBO) == 160, "GeometryFrameUBO");

	Ref<RHIBuffer> m_DrawUBO  = RHIBuffer::Create(BufferDesc{ sizeof(GeometryDrawUBO),  BufferUsage::Uniform, true, nullptr });
	Ref<RHIBuffer> m_FrameUBO = RHIBuffer::Create(BufferDesc{ sizeof(GeometryFrameUBO), BufferUsage::Uniform, true, nullptr });

	// The pass has two UBOs that share shader binding 0 — one per-frame
	// (light/view), one per-draw (model/viewproj). On GL, applying a desc set
	// is just glBindBufferBase, so applying the draw UBO after the frame UBO
	// at the same binding gives the draw UBO priority (the last writer wins).
	// On Vulkan this would need two descriptor sets bound at two different
	// binding slots, which requires splitting the PerFrame_Geometry UBO into
	// a separate set; deferred until a Vulkan backend exists.
	Ref<RHIDescriptorSet> m_GeometryDescriptorSet;

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

		auto cmd = RHIRenderer::GetCmd();
		m_LitShader->Bind();
		cmd->BindTexture2D(13, resources.ShadowMapDepth);
		m_LitShader->SetUniform1i("u_ShadowMap", 13);

		// PerFrame_Geometry UBO upload.
		GeometryFrameUBO fubo;
		fubo.lightDir           = glm::vec4(lightDir, 0.0f);
		fubo.lightColor         = glm::vec4(lightColor, 0.0f);
		fubo.ambientStrength    = ambientStrength;
		fubo.viewPos            = glm::vec4(viewPos, 0.0f);
		fubo.u_LightViewProj    = resources.ShadowMapDepth
			? resources.ShadowLightViewProj
			: glm::mat4(1.0f);
		fubo.u_ShadowMapEnabled = resources.ShadowMapDepth ? 1 : 0;
		fubo.u_ShadowMapSize    = glm::vec2(2048.0f, 2048.0f);
		fubo.u_LightSize        = 50.0f;
		fubo.hasNormalMap       = 0;
		m_FrameUBO->Upload(&fubo, sizeof(fubo));

		// Apply the per-frame UBO at binding 0. The descriptor set issues
		// glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUBO), which is what
		// actually exposes the data to the shader — Upload() only writes
		// bytes, it does not bind to a slot.
		if (!m_GeometryDescriptorSet)
			m_GeometryDescriptorSet = RHIDescriptorSet::Create();
		m_GeometryDescriptorSet->Reset();
		m_GeometryDescriptorSet->BindUniformBuffer(0, m_FrameUBO);
		m_GeometryDescriptorSet->Apply(0);

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

				mat->Render(m_LitShader);

				// Per-draw UBO: 5 mat4s.
				GeometryDrawUBO dubo;
				dubo.MVP_matrix   = proj * view * modelMat;
				dubo.model        = modelMat;
				dubo.prevModel    = modelMat;
				dubo.viewProj     = currentViewProj;
				dubo.prevViewProj = m_PrevViewProjMatrix;
				m_DrawUBO->Upload(&dubo, sizeof(dubo));

				// Per-draw UBO at binding 0. With the per-frame UBO also at
				// binding 0, the GL backend only sees the last apply — so we
				// re-bind both, with the draw UBO last (matches the order in
				// the legacy path which wrote draw UBO immediately before draw).
				// The Vulkan backend will need two separate descriptor sets
				// for that to be correct; see the comment on the struct.
				m_GeometryDescriptorSet->Reset();
				m_GeometryDescriptorSet->BindUniformBuffer(0, m_DrawUBO);
				m_GeometryDescriptorSet->Apply(0);

				mesh.gpuMesh->Draw();
				drewSomething = true;
			}
		}

		if (!drewSomething)
		{
			m_LitShader->Bind();
			{
				// Fallback cube path. The sampler bindings (10/11/12) are
				// still whatever the previous frame left, so pin them to
				// 1x1 white before drawing — otherwise the cube samples
				// stale history color or shadow mask and renders as
				// random colors that drift with the camera.
				RHI_DefaultTex->Bind(10);
				RHI_DefaultTex->Bind(11);
				RHI_DefaultTex->Bind(12);

				// Draw cube (pipeline binds shader — set MVP first)
				mat4 model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
				mat4 mvp = proj * view * model;

				GeometryDrawUBO dubo;
				dubo.MVP_matrix   = mvp;
				dubo.model        = model;
				dubo.prevModel    = model;
				dubo.viewProj     = currentViewProj;
				dubo.prevViewProj = m_PrevViewProjMatrix;
				m_DrawUBO->Upload(&dubo, sizeof(dubo));

				m_GeometryDescriptorSet->Reset();
				m_GeometryDescriptorSet->BindUniformBuffer(0, m_DrawUBO);
				m_GeometryDescriptorSet->Apply(0);

				cmd->BindPipeline(m_CubePipeline);
				cmd->DrawIndexed(36);  // cube

				// Draw floor plane (pipeline still bound — set floor MVP now)
				mat4 floorModel = translate(mat4(1.0f), vec3(0.0f, -2.0f, 0.0f));
				floorModel = scale(floorModel, vec3(10.0f, 0.05f, 10.0f));
				mat4 floorMVP = proj * view * floorModel;
				dubo.MVP_matrix   = floorMVP;
				dubo.model        = floorModel;
				dubo.prevModel    = floorModel;
				m_DrawUBO->Upload(&dubo, sizeof(dubo));

				m_GeometryDescriptorSet->Reset();
				m_GeometryDescriptorSet->BindUniformBuffer(0, m_DrawUBO);
				m_GeometryDescriptorSet->Apply(0);

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
				cmd.SetDepthTest(true);
				cmd.SetDepthFunc(CompareOp::Less);

				// Skybox first, matching the legacy forward chain, which drew it
				// into the bound FBO before the geometry pass ran. It masks depth
				// writes internally and restores them, leaving the state above intact.
				currentcamera->RenderSkyBox();

				// PerFrame_Geometry UBO. Uniform paths no longer drive these
				// fields — they live in PerFrame_Geometry now — so the only
				// way to feed them is via the UBO + descriptor set.
				RHITexture2D* shadowTex = hasShadow ? resources.GetTexture(shadowDepth) : nullptr;

				GeometryFrameUBO fubo;
				fubo.lightDir           = glm::vec4(lightDir, 0.0f);
				fubo.lightColor         = glm::vec4(lightColor, 0.0f);
				fubo.ambientStrength    = ambientStrength;
				fubo.viewPos            = glm::vec4(viewPos, 0.0f);
				fubo.u_LightViewProj    = shadowTex
					? frameData->ShadowLightViewProj
					: glm::mat4(1.0f);
				fubo.u_ShadowMapEnabled = shadowTex ? 1 : 0;
				fubo.u_ShadowMapSize    = glm::vec2(2048.0f, 2048.0f);
				fubo.u_LightSize        = 50.0f;
				fubo.hasNormalMap       = 0;
				m_FrameUBO->Upload(&fubo, sizeof(fubo));

				// Bind shader before applying the descriptor set so the next
				// glBindBufferBase lands on a program that reads it.
				m_LitShader->Bind();

				if (!m_GeometryDescriptorSet)
					m_GeometryDescriptorSet = RHIDescriptorSet::Create();
				m_GeometryDescriptorSet->Reset();
				m_GeometryDescriptorSet->BindUniformBuffer(0, m_FrameUBO);
				m_GeometryDescriptorSet->Apply(0);

				// u_ShadowMap is a sampler at binding 13, not a UBO field.
				cmd.BindTexture2D(13, shadowTex ? shadowTex->GetNativeID() : 0);

				bool drewSomething = false;
				RHI_DefaultTex->Bind(0);

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

						// Per-draw UBO; rebind at binding 0 so the draw
						// matrices reach the shader before the draw call.
						GeometryDrawUBO dubo;
						dubo.MVP_matrix   = proj * view * modelMat;
						dubo.model        = modelMat;
						dubo.prevModel    = modelMat;
						dubo.viewProj     = currentViewProj;
						dubo.prevViewProj = m_PrevViewProjMatrix;
						m_DrawUBO->Upload(&dubo, sizeof(dubo));

						m_GeometryDescriptorSet->Reset();
						m_GeometryDescriptorSet->BindUniformBuffer(0, m_DrawUBO);
						m_GeometryDescriptorSet->Apply(0);

						mesh.gpuMesh->Draw();
						drewSomething = true;
					}
				}

				if (!drewSomething)
				{
					m_LitShader->Bind();

					// Per-frame UBO again (the mesh loop above would have
					// rebound the draw UBO if it ran).
					m_GeometryDescriptorSet->Reset();
					m_GeometryDescriptorSet->BindUniformBuffer(0, m_FrameUBO);
					m_GeometryDescriptorSet->Apply(0);

					// DEBUG: dump UBO binding state
					GLint bound0 = 0;
					glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &bound0);
					GLint litProg = 0;
					glGetIntegerv(GL_CURRENT_PROGRAM, &litProg);
					Info_Core("CUBE fallback: gl UBO slot0={}, litProg={}", bound0, litProg);

					// Dump the actual draw UBO bytes. If `model` is being
					// reuploaded with anything other than the identity scale,
					// the normal interpolation will skew.
					GLint prog = 0;
					glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
					if (prog != 0)
					{
						// Re-upload the draw UBO RIGHT NOW so we know bytes
						// at this exact debug point reflect what the shader
						// will see at the draw call.
						mat4 md = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
						GeometryDrawUBO dd;
						dd.MVP_matrix   = proj * view * md;
						dd.model        = md;
						dd.prevModel    = md;
						dd.viewProj     = currentViewProj;
						dd.prevViewProj = m_PrevViewProjMatrix;
						m_DrawUBO->Upload(&dd, sizeof(dd));

						// Dump CPU-side first
						Info_Core("  CPU-side dd:");
						Info_Core("    MVP col 0: [{},{},{},{}]", dd.MVP_matrix[0][0], dd.MVP_matrix[0][1], dd.MVP_matrix[0][2], dd.MVP_matrix[0][3]);
						Info_Core("    Model col 0: [{},{},{},{}]", dd.model[0][0], dd.model[0][1], dd.model[0][2], dd.model[0][3]);
						Info_Core("    Model col 1: [{},{},{},{}]", dd.model[1][0], dd.model[1][1], dd.model[1][2], dd.model[1][3]);
						Info_Core("    Model col 2: [{},{},{},{}]", dd.model[2][0], dd.model[2][1], dd.model[2][2], dd.model[2][3]);
						Info_Core("    Model col 3: [{},{},{},{}]", dd.model[3][0], dd.model[3][1], dd.model[3][2], dd.model[3][3]);

						m_GeometryDescriptorSet->Reset();
						m_GeometryDescriptorSet->BindUniformBuffer(0, m_DrawUBO);
						m_GeometryDescriptorSet->Apply(0);

						GLint boundDraw = 0;
						glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &boundDraw);
						glBindBuffer(GL_UNIFORM_BUFFER, boundDraw);
						float* p = (float*)glMapBufferRange(GL_UNIFORM_BUFFER, 0, sizeof(GeometryDrawUBO), GL_MAP_READ_BIT);
						if (p)
						{
							Info_Core("  GPU drawUBO:");
							Info_Core("    MVP col 0   : [{},{},{},{}]", p[0], p[1], p[2], p[3]);
							Info_Core("    Model col 0: [{},{},{},{}]", p[16], p[17], p[18], p[19]);
							Info_Core("    Model col 1: [{},{},{},{}]", p[20], p[21], p[22], p[23]);
							Info_Core("    Model col 2: [{},{},{},{}]", p[24], p[25], p[26], p[27]);
							Info_Core("    Model col 3: [{},{},{},{}]", p[28], p[29], p[30], p[31]);
							glUnmapBuffer(GL_UNIFORM_BUFFER);
						}
						glBindBuffer(GL_UNIFORM_BUFFER, 0);
					}
					// Fallback cube path has no material, so the diffuse /
					// specular / normal samplers would otherwise sample
					// whatever was last bound to units 10/11/12 — leftover
					// from the previous frame's history color, shadow mask,
					// etc. Pin them to the 1x1 white so the cube renders
					// solid white and lighting math has a stable input.
					RHI_DefaultTex->Bind(10);
					RHI_DefaultTex->Bind(11);
					RHI_DefaultTex->Bind(12);

					mat4 model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
					mat4 mvp = proj * view * model;

					GeometryDrawUBO dubo;
					dubo.MVP_matrix   = mvp;
					dubo.model        = model;
					dubo.prevModel    = model;
					dubo.viewProj     = currentViewProj;
					dubo.prevViewProj = m_PrevViewProjMatrix;
					m_DrawUBO->Upload(&dubo, sizeof(dubo));

					m_GeometryDescriptorSet->Reset();
					m_GeometryDescriptorSet->BindUniformBuffer(0, m_DrawUBO);
					m_GeometryDescriptorSet->Apply(0);

					cmd.BindPipeline(m_CubePipeline);
					cmd.DrawIndexed(36);

					mat4 floorModel = translate(mat4(1.0f), vec3(0.0f, -2.0f, 0.0f));
					floorModel = scale(floorModel, vec3(10.0f, 0.05f, 10.0f));
					mat4 floorMVP = proj * view * floorModel;
					dubo.MVP_matrix   = floorMVP;
					dubo.model        = floorModel;
					dubo.prevModel    = floorModel;
					m_DrawUBO->Upload(&dubo, sizeof(dubo));

					m_GeometryDescriptorSet->Reset();
					m_GeometryDescriptorSet->BindUniformBuffer(0, m_DrawUBO);
					m_GeometryDescriptorSet->Apply(0);

					cmd.DrawIndexed(36);

					m_CubePipeline->Unbind();
				}

				m_PrevViewProjMatrix = currentViewProj;
				m_FrameCount++;
			});

		return out;
	}

	void Init(Ref<RHIFramebuffer> fb)override {
		this->m_RHIGbuffer = fb;


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


