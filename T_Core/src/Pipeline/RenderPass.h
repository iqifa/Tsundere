#include<GLHead.h>
#include<HeadLine.h>
#include<Scene/Scene.h>
#include<Scene/Mesh.h>
#include<Scene/BVHBuilder.h>
#include<Panels/MeshFilePath.h>
#include<Panels/Material.h>
#include<DDGI/DDGI.h>
#include<set>
#include<Debug/Debug.h>
class Scene;
class  FrameBuffer;


struct  RenderResources
{
	// --- Legacy raw GL IDs (kept for backward compat during RHI migration) ---
	unsigned int SceneColorTexture = 0; // 上一阶段输出的场景颜色
	unsigned int VelocityTexture = 0;   // 上一阶段输出的运动矢量缓存 (Motion Vectors)
	unsigned int DepthTexture = 0;      // 深度图
	unsigned int ShadowMask = 0;
	unsigned int ShadowMapDepth = 0;  // directional-light shadow map depth texture (D32_SFLOAT)
	glm::mat4 ShadowLightViewProj = glm::mat4(1.0f); // light view-projection matrix (set by ShadowMapPass)

	// GBuffer textures (populated by GBufferPass, consumed by DeferredLightingPass)
	unsigned int GBufferPosition = 0;
	unsigned int GBufferNormal = 0;
	unsigned int GBufferAlbedo = 0;
	unsigned int GBufferSpecular = 0;        // 阴影遮罩纹理 (R8)

	// DDGI probe atlas textures
	unsigned int DDGIIrradianceAtlas = 0;
	unsigned int DDGIDepthAtlas = 0;

	// 渲染目标尺寸
	unsigned int SourceFBO = 0;        // 几何 Pass 的主 FBO（用于深度拷贝）

	// --- RHI handles (set by RHI-migrated passes, nullptr until migrated) ---
	// These coexist with the legacy raw IDs during the transition.
	// RHI-migrated passes set BOTH the RHI handle AND the raw ID (via GetNativeID()).
	// Non-migrated passes only set the raw ID, leaving RHI handles as nullptr.
	Ref<RHITexture2D> SceneColorRHI;   // RHI-backed scene color
	Ref<RHITexture2D> VelocityRHI;     // RHI-backed velocity / motion vectors
	Ref<RHITexture2D> DepthRHI;        // RHI-backed depth texture
	Ref<RHITexture2D> ShadowMaskRHI;   // RHI-backed shadow mask (R8)
	Ref<RHIFramebuffer> TargetFBO;     // RHI-backed render target FBO
};

class  RenderPass
{
public:
	virtual ~RenderPass() = default;

	virtual void Init(Ref<FrameBuffer>& m_GBuffer, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) {}

	virtual void Execute(Ref<Scene> scene, RenderResources& resources) = 0;
};

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
			m_LitShader->SetUniformMat4f("u_LightViewProj", resources.ShadowLightViewProj);
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, 0);
			m_LitShader->SetUniformMat4f("u_LightViewProj", glm::mat4(1.0f));
		}
		m_LitShader->SetUniform1i("u_ShadowMap", 5);

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

				renderer.DrawElement(*mesh.vao, *mesh.ibo, *m_LitShader);
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

				renderer.DrawElement(*mesh.vao, *mesh.ibo, *m_GBufferShader);
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





class TAAPass : public RenderPass
{
public:
	bool Enabled = true;

	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();

		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f
		};
		unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };

		m_QuadVA = CreatePtr<VertexArray>(4);
		m_QuadVB = CreatePtr<VertexBuffer>(quadVertices, sizeof(quadVertices));
		m_QuadIB = CreatePtr<IndexBuffer>(quadIndices, 6);
		VertexBufferLayout quadLayout;
		quadLayout.Push<float>(2);
		quadLayout.Push<float>(2);
		m_QuadVA->AddBuffer(*m_QuadVB, quadLayout);

		m_Shader = CreatePtr<Shader>("D:/Code/C++/Tsundere/res/shaders/TAA.shader");
	}

	void Execute(Ref<Scene>, RenderResources& resources) override
	{

		if (!Enabled) return;

		if (!m_HistoryFBOs[0])
		{
			m_HistoryFBOs[0] = CreatePtr<FrameBuffer>(m_Spec);
			m_HistoryFBOs[1] = CreatePtr<FrameBuffer>(m_Spec);
		}
		if (!m_PrevDepthFBO)
			m_PrevDepthFBO = CreatePtr<FrameBuffer>(m_Spec);

		int nextIdx = (m_CurrentIdx + 1) % 2;

		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 currentViewProj = proj * view;

		m_HistoryFBOs[nextIdx]->Bind();
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if (m_Shader)
		{
			m_Shader->Bind();




			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, resources.SceneColorTexture);
			m_Shader->SetUniform1i("u_CurrentColor", 0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, m_HistoryFBOs[m_CurrentIdx]->GetClolorAttachmentRenderID());
			m_Shader->SetUniform1i("u_HistoryColor", 1);

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, resources.VelocityTexture);
			m_Shader->SetUniform1i("u_VelocityTex", 2);

			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, resources.DepthTexture);
			m_Shader->SetUniform1i("u_DepthTex", 3);

			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D, m_PrevDepthFBO->GetDepthAttachmentRenderID());
			m_Shader->SetUniform1i("u_HistoryDepthTex", 4);

			m_Shader->SetUniformMat4f("u_InverseViewProj", glm::inverse(currentViewProj));
			m_Shader->SetUniformMat4f("u_PrevViewProj", m_PrevViewProj);

			Renderer renderer;
			renderer.DrawElement(*m_QuadVA, *m_QuadIB, *m_Shader);
		}

		m_HistoryFBOs[nextIdx]->UnBind();

		if (resources.SourceFBO)
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, resources.SourceFBO);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_PrevDepthFBO->GetFrameID());
			glBlitFramebuffer(0, 0, m_Spec.Width, m_Spec.Height,
				0, 0, m_Spec.Width, m_Spec.Height,
				GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		}

		m_PrevViewProj = currentViewProj;
		m_CurrentIdx = nextIdx;

		resources.SceneColorTexture = m_HistoryFBOs[m_CurrentIdx]->GetClolorAttachmentRenderID();
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
		if (m_HistoryFBOs[0]) m_HistoryFBOs[0]->Rsetsize({ w, h });
		if (m_HistoryFBOs[1]) m_HistoryFBOs[1]->Rsetsize({ w, h });
		if (m_PrevDepthFBO)  m_PrevDepthFBO->Rsetsize({ w, h });
	}

private:
	Ptr<FrameBuffer> m_HistoryFBOs[2];
	Ptr<FrameBuffer> m_PrevDepthFBO;
	Ptr<Shader> m_Shader;
	Ptr<VertexArray> m_QuadVA;
	Ptr<VertexBuffer> m_QuadVB;
	Ptr<IndexBuffer> m_QuadIB;

	int m_CurrentIdx = 0;
	mat4 m_PrevViewProj = mat4(1.0f);
	FrameBufferSpecification m_Spec;
};

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
		m_DepthShader = CreateRef<Shader>("D:/Code/C++/Tsundere/res/shaders/ShadowMapDepth.shader");
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
				renderer.DrawElement(*mesh.vao, *mesh.ibo, *m_DepthShader);
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
	Ref<Shader>      m_DepthShader;
	unsigned int     m_ShadowMapFBO   = 0;
	unsigned int     m_ShadowMapDepth = 0;
	Ptr<VertexArray> m_FallbackVA;
	Ptr<VertexBuffer>m_FallbackVB;
	Ptr<IndexBuffer> m_FallbackIB;
};

class ShadowPass : public RenderPass
{
public:
	bool Enabled = false;
	float LightDistance = 50.0f;

	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();
		m_Shader = Shader::CreateCompute("D:/Code/C++/Tsundere/res/shaders/ShadowRay.shader");
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
		m_Shader->SetUniformVec3("u_CameraPos", currentcamera->getpos());
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
	Ref<Shader> m_Shader;
	Ref<BVHBuilder> m_BVHBuilder;
	unsigned int m_ShadowMask = 0;
	glm::vec3 m_LightDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
};

class ShadowApplyPass : public RenderPass
{
public:
	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();

		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f
		};
		unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };

		m_QuadVA = CreatePtr<VertexArray>(4);
		m_QuadVB = CreatePtr<VertexBuffer>(quadVertices, sizeof(quadVertices));
		m_QuadIB = CreatePtr<IndexBuffer>(quadIndices, 6);
		VertexBufferLayout quadLayout;
		quadLayout.Push<float>(2);
		quadLayout.Push<float>(2);
		m_QuadVA->AddBuffer(*m_QuadVB, quadLayout);

		m_Shader = CreatePtr<Shader>("D:/Code/C++/Tsundere/res/shaders/ShadowApply.shader");
		CreateOutputTex(m_Spec.Width, m_Spec.Height);
	}

	void Execute(Ref<Scene>, RenderResources& resources) override
	{
		if (!resources.ShadowMask)
			return;

		// Bind intermediate FBO for output (avoids reading+writing same texture)
		glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
		glViewport(0, 0, m_Spec.Width, m_Spec.Height);

		m_Shader->Bind();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, resources.SceneColorTexture);
		m_Shader->SetUniform1i("u_SceneColor", 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, resources.ShadowMask);
		m_Shader->SetUniform1i("u_ShadowMask", 1);

		Renderer renderer;
		renderer.DrawElement(*m_QuadVA, *m_QuadIB, *m_Shader);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		resources.SceneColorTexture = m_OutputTex;
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
		CreateOutputTex(w, h);
	}

private:
	void CreateOutputTex(unsigned int w, unsigned int h)
	{
		if (m_OutputTex)
			glDeleteTextures(1, &m_OutputTex);
		if (m_OutputFBO)
			glDeleteFramebuffers(1, &m_OutputFBO);

		glGenTextures(1, &m_OutputTex);
		glBindTexture(GL_TEXTURE_2D, m_OutputTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenFramebuffers(1, &m_OutputFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTex, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	FrameBufferSpecification m_Spec;
	Ptr<Shader> m_Shader;
	Ptr<VertexArray> m_QuadVA;
	Ptr<VertexBuffer> m_QuadVB;
	Ptr<IndexBuffer> m_QuadIB;
	unsigned int m_OutputTex = 0;
	unsigned int m_OutputFBO = 0;
};

class PathTracePass : public RenderPass
{
public:
	bool Enabled = false;
	unsigned int MaxBounces = 4;

	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();

		m_Shader = Shader::CreateCompute("D:/Code/C++/Tsundere/res/shaders/PathTrace.shader");
		if (!m_Shader || m_Shader->GetID() == 0)
		{
			Error_Core("PathTracePass: Failed to create compute shader!");
			Enabled = false;
		}

		m_AccumTex[0] = ImageTexture::Create(m_Spec.Width, m_Spec.Height, GL_RGBA32F);
		m_AccumTex[1] = ImageTexture::Create(m_Spec.Width, m_Spec.Height, GL_RGBA32F);

		// Initial material SSBO (RebuildMaterialBuffer fills it properly)
		GPUMaterial defMat;
		defMat.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 0.5f);
		defMat.emission = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
		defMat.diffuseHandle = 0;
		m_MaterialSSBO = StorageBuffer::Create(sizeof(GPUMaterial), &defMat, 0);
	}

	// Rebuild the GPUMaterial SSBO from the scene's CPU materials.
	// Returns the Material* → global index mapping (also stored internally).
	void RebuildMaterialBuffer(Ref<Scene> scene)
	{
		m_MatToGlobalIndex.clear();
		m_GPUMaterials.clear();

		// Collect unique materials from all MeshRender entities
		for (auto [entityID, meshrender] : scene->m_Registry.view<Component::MeshRender>().each())
		{
			for (auto& mat : meshrender.materials)
			{
				if (!mat) continue;
				if (m_MatToGlobalIndex.find(mat.get()) != m_MatToGlobalIndex.end())
					continue;

				unsigned int idx = (unsigned int)m_GPUMaterials.size();
				m_MatToGlobalIndex[mat.get()] = idx;

				GPUMaterial gpu;

				// --- debug: dump material info once per material ---
				{
					static std::set<Material*> s_Logged;
					if (s_Logged.insert(mat.get()).second)
					{
						std::string info = "PathTrace mat#" + std::to_string(idx)
							+ " shader=" + mat->shader->GetPath() + " vars[";
						for (auto& v : mat->varies)
							info += " " + std::to_string((int)std::get<1>(v)) + ":" + std::get<2>(v);
						info += " ] texID=" + std::to_string(mat->texture.GetTextureID());
						Warn_Core(info);
					}
				}

				// Try to extract base color from known uniform names
				if (!ExtractBaseColor(mat, gpu.albedo))
					gpu.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
				gpu.emission = ExtractVec4(mat, "emission",
					glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));

				// Extract diffuse texture as bindless handle
				gpu.diffuseHandle = ExtractTextureHandle(mat);

				// If no base color AND no texture, derive from material index
				if (!ExtractBaseColor(mat, gpu.albedo) && gpu.diffuseHandle == 0)
				{
					float hue = float(idx) * 0.618033988749895f;
					hue = hue - std::floor(hue);
					gpu.albedo = glm::vec4(HsvToRgb(hue, 0.6f, 0.85f), 0.5f);
					//Warn_Core("PathTrace mat#" + std::to_string(idx) + " -> HSV fallback");
				}

				m_GPUMaterials.push_back(gpu);
			}
		}

		// Ensure at least one material
		if (m_GPUMaterials.empty())
		{
			GPUMaterial defMat;
			defMat.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 0.5f);
			defMat.emission = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
			defMat.diffuseHandle = 0;
			m_GPUMaterials.push_back(defMat);
		}

		// Upload
		m_MaterialSSBO = StorageBuffer::Create(
			m_GPUMaterials.size() * sizeof(GPUMaterial),
			m_GPUMaterials.data(), 0);
	}

	void Execute(Ref<Scene> scene, RenderResources& resources) override
	{
		if (!Enabled)
		{
			resources.SceneColorTexture = m_AccumTex[m_CurrentIdx]->GetID();
			return;
		}

		// Rebuild material SSBO every frame (cheap, picks up UI uniform changes)
		RebuildMaterialBuffer(scene);

		// Rebuild BVH only when scene geometry changed (dirty-flag).
		// Pass material map so triangles get global material indices.
		if (m_BVHBuilder && m_BVHBuilder->IsDirty())
		{
			m_BVHBuilder->SetMaterialMap(&m_MatToGlobalIndex);
			m_BVHBuilder->GatherTriangles(scene);
			m_BVHBuilder->BuildBVH(4);
			m_BVHBuilder->UploadToGPU();
			m_BVHBuilder->MarkClean();
			m_BVHBuilder->SetMaterialMap(nullptr);
		}

		// Reset accumulation on camera movement or viewport change
		vec3 camPos = currentcamera->getpos();
		if (glm::distance(camPos, m_LastCamPos) > 0.01f ||
			m_LastViewportSize.x != (float)m_Spec.Width ||
			m_LastViewportSize.y != (float)m_Spec.Height)
		{
			m_SampleCount = 0;
			m_LastCamPos = camPos;
			m_LastViewportSize = glm::vec2((float)m_Spec.Width, (float)m_Spec.Height);
		}

		m_Shader->Bind();
		if (m_Shader->GetID() == 0)
		{
			resources.SceneColorTexture = m_AccumTex[m_CurrentIdx]->GetID();
			return;
		}

		// Accumulation ping-pong
		if (m_SampleCount > 0)
			m_AccumTex[m_CurrentIdx]->Bind(0, GL_READ_ONLY);
		m_AccumTex[1 - m_CurrentIdx]->Bind(1, GL_WRITE_ONLY);

		// BVH SSBOs
		if (!m_BVHBuilder || !m_BVHBuilder->GetTriangleBuffer() || !m_BVHBuilder->GetBVHNodeBuffer())
		{
			resources.SceneColorTexture = m_AccumTex[m_CurrentIdx]->GetID();
			return;
		}
		m_BVHBuilder->GetTriangleBuffer()->BindToSlot(3);
		m_BVHBuilder->GetBVHNodeBuffer()->BindToSlot(4);

		// Material SSBO
		m_MaterialSSBO->BindToSlot(5);

		// Skybox cubemap
		if (currentcamera && currentcamera->skybox && currentcamera->skybox->m_Cmp)
		{
			glActiveTexture(GL_TEXTURE6);
			glBindTexture(GL_TEXTURE_CUBE_MAP, currentcamera->skybox->m_Cmp->GetMap());
			m_Shader->SetUniform1i("u_SkyBox", 6);
		}

		// Camera uniforms
		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		m_Shader->SetUniformMat4f("u_InvView", glm::inverse(view));
		m_Shader->SetUniformMat4f("u_InvProj", glm::inverse(proj));
		m_Shader->SetUniformVec3("u_CameraPos", camPos);
		m_Shader->SetUniformVec2("u_Resolution", glm::vec2((float)m_Spec.Width, (float)m_Spec.Height));
		m_Shader->SetUniform1f("u_SampleIndex", (float)m_SampleCount);
		m_Shader->SetUniform1f("u_FrameSeed", (float)m_FrameIdx);

		if (m_SampleCount > 0)
			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

		unsigned int gx = (m_Spec.Width + 7) / 8;
		unsigned int gy = (m_Spec.Height + 7) / 8;
		m_Shader->DispatchCompute(gx, gy);

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

		// Unbind image units so ImGui can safely sample the texture
		glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
		glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);

		// Swap ping-pong
		m_CurrentIdx = 1 - m_CurrentIdx;
		m_SampleCount++;
		m_FrameIdx++;

		// Output to display
		resources.SceneColorTexture = m_AccumTex[m_CurrentIdx]->GetID();
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
		if (m_AccumTex[0]) m_AccumTex[0]->Resize(w, h);
		if (m_AccumTex[1]) m_AccumTex[1]->Resize(w, h);
		m_SampleCount = 0;
	}

	void SetBVHBuilder(Ref<BVHBuilder> builder)
	{
		m_BVHBuilder = builder;
		// Force BVH rebuild on first frame: ShadowPass built the BVH without
		// material mapping, so triangle matIdx values are mesh-local (wrong).
		if (m_BVHBuilder)
			m_BVHBuilder->MarkDirty();
	}
	void ResetAccumulation() { m_SampleCount = 0; }
	unsigned int GetSampleCount() const { return m_SampleCount; }

private:
	// Extract uniform value from Material::varies by name
	bool HasUniform(Ref<Material> mat, const std::string& name) const
	{
		for (auto& var : mat->varies)
			if (std::get<2>(var) == name)
				return true;
		return false;
	}

	float ExtractFloat(Ref<Material> mat, const std::string& name, float defVal) const
	{
		for (auto& var : mat->varies)
		{
			if (std::get<2>(var) == name)
			{
				if (std::get<1>(var) == ValueType::FLOAT)
					return *(float*)std::get<0>(var);
				if (std::get<1>(var) == ValueType::DOUBLE)
					return (float)*(double*)std::get<0>(var);
				if (std::get<1>(var) == ValueType::INT)
					return (float)*(int*)std::get<0>(var);
			}
		}
		return defVal;
	}

	glm::vec3 ExtractVec3(Ref<Material> mat, const std::string& name, glm::vec3 defVal) const
	{
		for (auto& var : mat->varies)
			if (std::get<2>(var) == name && std::get<1>(var) == ValueType::VEC3)
				return *(glm::vec3*)std::get<0>(var);
		return defVal;
	}

	glm::vec4 ExtractVec4(Ref<Material> mat, const std::string& name, glm::vec4 defVal) const
	{
		for (auto& var : mat->varies)
			if (std::get<2>(var) == name && std::get<1>(var) == ValueType::VEC3)
				return glm::vec4(*(glm::vec3*)std::get<0>(var), defVal.a);
		return defVal;
	}

	// Try to extract base color from known uniform names.
	// Returns false if nothing found → caller uses fallback.
	bool ExtractBaseColor(Ref<Material> mat, glm::vec4& outColor) const
	{
		static const char* kNames[] = { "albedo", "color", "baseColor", "diffuseColor", "tint", "diffuse" };
		for (auto name : kNames)
		{
			for (auto& var : mat->varies)
			{
				if (std::get<2>(var) != name) continue;
				if (std::get<1>(var) == ValueType::VEC3)
					{ outColor = glm::vec4(*(glm::vec3*)std::get<0>(var), 0.5f); return true; }
				if (std::get<1>(var) == ValueType::VEC2)
					{ glm::vec2 v = *(glm::vec2*)std::get<0>(var); outColor = glm::vec4(v, 0.0f, 0.5f); return true; }
				if (std::get<1>(var) == ValueType::FLOAT)
					{ float v = *(float*)std::get<0>(var); outColor = glm::vec4(v, v, v, 0.5f); return true; }
			}
		}
		return false;
	}

	// Extract diffuse texture as bindless handle (ARB_bindless_texture).
	// Returns 0 if no valid texture found → shader falls back to albedo color.  
	GLuint64 ExtractTextureHandle(Ref<Material> mat) const
	{
		static bool s_Checked = false, s_HasBindless = false;
		if (!s_Checked)
		{
			s_HasBindless = GLEW_ARB_bindless_texture != GL_FALSE;
			s_Checked = true;
			if (!s_HasBindless)
				Warn_Core("PathTrace: GL_ARB_bindless_texture not available");
		}
		if (!s_HasBindless)
			return 0;

		// 1) Try the material's direct texture member — only if loaded (default Texture
		//    has uninitialized m_RendererID, so check GetPath() first to skip garbage)
		unsigned int texID = 0;
		if (!mat->texture.GetPath().empty())
			texID = mat->texture.GetTextureID();
		if (texID == 0)
		{
			// 2) Try the first "texture_diffuse1" in varies
			for (auto& var : mat->varies)
			{
				if (std::get<1>(var) == ValueType::TEXTURE &&
					std::get<2>(var).find("diffuse") != std::string::npos)
				{
					Texture* t = (Texture*)std::get<0>(var);
					texID = t->GetTextureID();
					break;
				}
			}
		}
		// 3) Fallback: any texture in varies
		if (texID == 0)
		{
			for (auto& var : mat->varies)
			{
				if (std::get<1>(var) == ValueType::TEXTURE)
				{
					Texture* t = (Texture*)std::get<0>(var);
					texID = t->GetTextureID();
					break;
				}
			}
		}

		if (texID == 0)
			return 0;

		// Obtain bindless handle and make resident
		GLuint64 handle = glGetTextureHandleARB(texID);
		if (handle == 0)
			return 0;

		glMakeTextureHandleResidentARB(handle);
		return handle;
	}

	static glm::vec3 HsvToRgb(float h, float s, float v)
	{
		float c = v * s;
		float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
		float m = v - c;
		glm::vec3 rgb;
		if (h < 1.0f / 6.0f)      rgb = glm::vec3(c, x, 0.0f);
		else if (h < 2.0f / 6.0f) rgb = glm::vec3(x, c, 0.0f);
		else if (h < 3.0f / 6.0f) rgb = glm::vec3(0.0f, c, x);
		else if (h < 4.0f / 6.0f) rgb = glm::vec3(0.0f, x, c);
		else if (h < 5.0f / 6.0f) rgb = glm::vec3(x, 0.0f, c);
		else                       rgb = glm::vec3(c, 0.0f, x);
		return rgb + glm::vec3(m);
	}

	FrameBufferSpecification m_Spec;
	Ref<Shader> m_Shader;
	Ref<ImageTexture> m_AccumTex[2];
	int m_CurrentIdx = 0;
	Ref<BVHBuilder> m_BVHBuilder;
	Ref<StorageBuffer> m_MaterialSSBO;

	std::vector<GPUMaterial> m_GPUMaterials;
	std::unordered_map<Material*, unsigned int> m_MatToGlobalIndex;

	unsigned int m_SampleCount = 0;
	unsigned int m_FrameIdx = 0;
	glm::vec3 m_LastCamPos = glm::vec3(FLT_MAX);
	glm::vec2 m_LastViewportSize = glm::vec2(0.0f);
};

// ============================================================================
// DDGIPass — Dynamic Diffuse Global Illumination probe update
// ============================================================================
// Casts rays from a 3D grid of probes into the scene using the BVH.
// Accumulates irradiance into an octahedral-map texture atlas with
// temporal blending (ping-pong history). Probes are updated round-robin.
class  DDGIPass : public RenderPass
{
public:
	bool Enabled = true;

	// Volume configuration
	glm::ivec3 GridSize = { 8, 4, 8 };
	float     Spacing = 2.0f;
	float     ProbeRadius = 3.0f;
	int       RaysPerProbe = 256;
	int       ProbesPerUpdate = 128;
	float     Hysteresis = 0.97f;
	float     DepthSharpness = 50.0f;
	bool      ScrollWithCamera = true;
	glm::vec3 GridOrigin = glm::vec3(0.0f);
	bool      AutoPlaceGrid = true;


	Ref<StorageBuffer>m_MaterialSSBO;

	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();
		m_UpdateShader = Shader::CreateCompute("D:/Code/C++/Tsundere/res/shaders/DDGIProbeUpdate.shader");
		if (!m_UpdateShader || m_UpdateShader->GetID() == 0)
		{
			Error_Core("DDGIPass: Failed to create DDGIProbeUpdate compute shader!");
			Enabled = false;
		}
		RebuildAtlases();
	}

	void Execute(Ref<Scene> scene, RenderResources& resources) override
	{
		if (!Enabled || !m_BVHBuilder)
			return;

		// Rebuild atlas/SSBO if grid size changed
		if (GridSize != m_LastGridSize)
		{
			RebuildAtlases();
			m_LastGridSize = GridSize;
		}

		// Rebuild BVH only when scene geometry changed
		if (m_BVHBuilder->IsDirty())
		{
			m_BVHBuilder->GatherTriangles(scene);
			m_BVHBuilder->BuildBVH(4);
			m_BVHBuilder->UploadToGPU();
			m_BVHBuilder->MarkClean();
		}

		if (!m_BVHBuilder->GetTriangleBuffer() || !m_BVHBuilder->GetBVHNodeBuffer())
			return;

		// Update camera scroll
		if (ScrollWithCamera && AutoPlaceGrid && currentcamera)
			UpdateScrollOffset();

		// Upload current probe positions
		UpdateProbeRayDataSSBO();

		m_UpdateShader->Bind();

		// Bind probe ray data SSBO (slot 0)
		m_ProbeRayDataSSBO->BindToSlot(0);

		// Bind output atlases as images (write-only)
		m_IrradianceAtlas->Bind(1, GL_WRITE_ONLY);
		m_DepthAtlas->Bind(2, GL_WRITE_ONLY);

		// Bind history atlases (read-only, for temporal blend)
		if (m_FrameIdx > 0)
		{
			m_IrradianceAtlasPrev->Bind(3, GL_READ_ONLY);
			m_DepthAtlasPrev->Bind(4, GL_READ_ONLY);
		}
		else
		{
			// First frame: no history, bind current as both
			m_IrradianceAtlas->Bind(3, GL_READ_ONLY);
			m_DepthAtlas->Bind(4, GL_READ_ONLY);
		}

		// Bind BVH SSBOs
		m_BVHBuilder->GetTriangleBuffer()->BindToSlot(5);
		m_BVHBuilder->GetBVHNodeBuffer()->BindToSlot(6);

			// Build and bind material SSBO (slot 7)
			BuildMaterialSSBO(scene);
			m_MaterialSSBO->BindToSlot(7);

		// Bind skybox cubemap
		glActiveTexture(GL_TEXTURE7);
		if (currentcamera && currentcamera->skybox && currentcamera->skybox->m_Cmp)
			glBindTexture(GL_TEXTURE_CUBE_MAP, currentcamera->skybox->m_Cmp->GetMap());
		m_UpdateShader->SetUniform1i("u_SkyBox", 7);

		// Uniforms
		m_UpdateShader->SetUniform1i("u_TotalProbes", m_TotalProbes);
		m_UpdateShader->SetUniform1i("u_ProbesPerRow", m_ProbesPerRow);
		m_UpdateShader->SetUniform1i("u_RaysPerProbe", RaysPerProbe);
			m_UpdateShader->SetUniform1i("u_ProbesPerUpdate", ProbesPerUpdate);
		m_UpdateShader->SetUniform1i("u_ProbeOffset", m_CurrentProbeOffset);
		m_UpdateShader->SetUniform1f("u_Hysteresis", Hysteresis);
		m_UpdateShader->SetUniform1f("u_FrameSeed", (float)m_FrameIdx);

		// Dispatch: ProbesPerUpdate * RaysPerProbe threads
		int totalRays = ProbesPerUpdate * RaysPerProbe;
		int groups = (totalRays + 63) / 64;
		m_UpdateShader->DispatchCompute(groups);

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

		// Unbind images so they can be sampled as textures later
		glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
		glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
		glBindImageTexture(3, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
		glBindImageTexture(4, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);

		// Advance round-robin
		m_CurrentProbeOffset = (m_CurrentProbeOffset + ProbesPerUpdate) % m_TotalProbes;
			// Log to confirm DDGI is active
			if (m_FrameIdx % 60 == 0)
						Info_Core("DDGI: frame {}, updating {}/{} probes, atlas {}x{}", m_FrameIdx, ProbesPerUpdate, m_TotalProbes, m_IrradianceAtlasPrev->GetWidth(), m_IrradianceAtlasPrev->GetHeight());
		m_FrameIdx++;

		// Ping-pong: swap current and history atlases
		std::swap(m_IrradianceAtlas, m_IrradianceAtlasPrev);
		std::swap(m_DepthAtlas, m_DepthAtlasPrev);

		// Store outputs in RenderResources
		resources.DDGIIrradianceAtlas = m_IrradianceAtlasPrev->GetID();
		resources.DDGIDepthAtlas = m_DepthAtlasPrev->GetID();
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
	}

	void SetBVHBuilder(Ref<BVHBuilder> builder)
	{
		m_BVHBuilder = builder;
	}

		// Reset all accumulated irradiance (clears atlas, restarts round-robin)
		// Does NOT change probe positions or grid origin.
		void Reset()
		{
			// Recreate atlas textures (clears to black)
			int irradianceAtlasW = m_ProbesPerRow * 8;
			int irradianceAtlasH = ((m_TotalProbes + m_ProbesPerRow - 1) / m_ProbesPerRow) * 8;
			int depthAtlasW = m_ProbesPerRow * 16;
			int depthAtlasH = ((m_TotalProbes + m_ProbesPerRow - 1) / m_ProbesPerRow) * 16;

			m_IrradianceAtlas     = ImageTexture::Create(irradianceAtlasW, irradianceAtlasH, GL_RGBA16F);
			m_IrradianceAtlasPrev = ImageTexture::Create(irradianceAtlasW, irradianceAtlasH, GL_RGBA16F);
			m_DepthAtlas          = CreateRef<ImageTexture>(depthAtlasW, depthAtlasH, GL_R16F, GL_RED, GL_FLOAT);
			m_DepthAtlasPrev      = CreateRef<ImageTexture>(depthAtlasW, depthAtlasH, GL_R16F, GL_RED, GL_FLOAT);

			m_ScrollOffset        = glm::ivec3(0);
			m_CurrentProbeOffset  = 0;
			m_FrameIdx            = 0;
		}


	// Accessors for DeferredLightingPass
	unsigned int GetIrradianceAtlasID() const { return m_IrradianceAtlasPrev ? m_IrradianceAtlasPrev->GetID() : 0; }
	unsigned int GetDepthAtlasID() const { return m_DepthAtlasPrev ? m_DepthAtlasPrev->GetID() : 0; }
	int GetProbesPerRow() const { return m_ProbesPerRow; }
	const glm::ivec3& GetGridSize() const { return GridSize; }
	float GetSpacing() const { return Spacing; }
	float GetProbeRadius() const { return ProbeRadius; }
	float GetDepthSharpness() const { return DepthSharpness; }
	glm::vec3 GetGridOrigin() const { return GridOrigin + glm::vec3(m_ScrollOffset) * Spacing; }
	int GetTotalProbes() const { return m_TotalProbes; }

		// Probe debug visualization
		bool ShowProbes = false;
		void RenderProbeDebug(mat4 view, mat4 proj)
		{
			if (!ShowProbes || m_TotalProbes == 0) return;
			if (!m_ProbeVisShader)
				m_ProbeVisShader = CreatePtr<Shader>("D:/Code/C++/Tsundere/res/shaders/ProbeVis.shader");
			if (!m_ProbeVisVA)
				BuildProbeVisGeometry();

			mat4 vp = proj * view;
			m_ProbeVisShader->Bind();
			glDisable(GL_DEPTH_TEST);
			glPointSize(10.0f);

			// Draw probe points
			m_ProbeVisShader->SetUniformVec3("u_Color", glm::vec3(0.0f, 1.0f, 0.5f));
			for (int i = 0; i < m_TotalProbes; i++)
			{
				glm::vec3 pos = DDGI::ProbeWorldPos(GridSize, GridOrigin, Spacing, m_ScrollOffset, i);
				mat4 model = glm::translate(glm::mat4(1.0f), pos);
				m_ProbeVisShader->SetUniformMat4f("u_MVP", vp * model);
				m_ProbeVisVA->Bind();
					glDrawArrays(GL_POINTS, 0, 1);
			}

			glPointSize(1.0f);
			glEnable(GL_DEPTH_TEST);
			m_ProbeVisShader->UnBind();
		}

private:
	void BuildMaterialSSBO(Ref<Scene> scene) {
		std::vector<GPUMaterial> materials;

		for (auto [entityID,transform, meshrender] : scene->m_Registry.view<Component::Transform, Component::MeshRender>().each())
		{
			for (auto& mat : meshrender.materials)
			{
				if (!mat) continue;
				GPUMaterial gpu;
				gpu.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 0.5f);
				gpu.emission = glm::vec4(0.0f);
				gpu.diffuseHandle = 0;

				for (auto& v : mat->varies) {
					if (std::get<2>(v) == "albedo" || std::get<2>(v) == "color" || std::get<2>(v) == "baseColor") {
						if (std::get<1>(v) == ValueType::VEC3)
							gpu.albedo = glm::vec4(*(glm::vec3*)std::get<0>(v), 0.5f);
						else if (std::get<1>(v) == ValueType::FLOAT) {
							float vv = *(float*)std::get<0>(v);
							gpu.albedo = glm::vec4(vv, vv, vv, 0.5f);
						}
					}
				}

				// Extract diffuse texture handle (bindless)
				if (!mat->texture.GetPath().empty()) {
					unsigned int texID = mat->texture.GetTextureID();
					if (texID && GLEW_ARB_bindless_texture) {
						GLuint64 handle = glGetTextureHandleARB(texID);
						if (handle) {
							glMakeTextureHandleResidentARB(handle);
							gpu.diffuseHandle = handle;
						}
					}
				}
				materials.push_back(gpu);
			}


		}

		if (materials.empty()) {
			GPUMaterial def;
			def.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 0.5f);
			def.emission = glm::vec4(0.0f);
			def.diffuseHandle = 0;
			materials.push_back(def);
		}
		m_MaterialSSBO = StorageBuffer::Create(materials.size() * sizeof(GPUMaterial), materials.data(), 0);
	}

	void RebuildAtlases()
	{
		m_TotalProbes = GridSize.x * GridSize.y * GridSize.z;
		m_ProbesPerRow = DDGI::ComputeProbesPerRow(m_TotalProbes);

		int irradianceAtlasW = m_ProbesPerRow * 8;
		int irradianceAtlasH = ((m_TotalProbes + m_ProbesPerRow - 1) / m_ProbesPerRow) * 8;
		int depthAtlasW = m_ProbesPerRow * 16;
		int depthAtlasH = ((m_TotalProbes + m_ProbesPerRow - 1) / m_ProbesPerRow) * 16;

		m_IrradianceAtlas = ImageTexture::Create(irradianceAtlasW, irradianceAtlasH, GL_RGBA16F);
		m_IrradianceAtlasPrev = ImageTexture::Create(irradianceAtlasW, irradianceAtlasH, GL_RGBA16F);
		m_DepthAtlas = CreateRef<ImageTexture>(depthAtlasW, depthAtlasH, GL_R16F, GL_RED, GL_FLOAT);
		m_DepthAtlasPrev = CreateRef<ImageTexture>(depthAtlasW, depthAtlasH, GL_R16F, GL_RED, GL_FLOAT);

		// Allocate probe ray data SSBO
		m_ProbeRayDataSSBO = StorageBuffer::Create(
			m_TotalProbes * sizeof(DDGI::GPUProbeRayData), nullptr, 0);

		if (AutoPlaceGrid)
		{
			GridOrigin = currentcamera ? currentcamera->getpos()
				- glm::vec3(GridSize) * Spacing * 0.5f
				: glm::vec3(-16.0f, -4.0f, -16.0f);
		}

		m_ScrollOffset = glm::ivec3(0);
		m_CurrentProbeOffset = 0;
		m_FrameIdx = 0;
	}

	void UpdateProbeRayDataSSBO()
	{
		std::vector<DDGI::GPUProbeRayData> probeData;
		DDGI::BuildProbeRayData(probeData, GridSize, GridOrigin, Spacing, ProbeRadius, m_ScrollOffset);
		m_ProbeRayDataSSBO->SetData(probeData.data(),
			probeData.size() * sizeof(DDGI::GPUProbeRayData), 0);
	}

	void UpdateScrollOffset()
	{
		glm::vec3 camPos = currentcamera->getpos();
		glm::ivec3 newScroll(
			(int)std::floor(camPos.x / Spacing) - GridSize.x / 2,
			(int)std::floor(camPos.y / Spacing) - GridSize.y / 2,
			(int)std::floor(camPos.z / Spacing) - GridSize.z / 2);

		if (newScroll != m_ScrollOffset)
		{
			// Scroll: reset hysteresis for probes newly scrolled in
			// (simplified: swap clears history)
			m_ScrollOffset = newScroll;
			m_FrameIdx = 0;
		}
	}

	FrameBufferSpecification m_Spec;
	Ref<Shader> m_UpdateShader;
	Ref<BVHBuilder> m_BVHBuilder;

	// Probe atlas textures (ping-pong for temporal blending)
	Ref<ImageTexture> m_IrradianceAtlas;
	Ref<ImageTexture> m_IrradianceAtlasPrev;
	Ref<ImageTexture> m_DepthAtlas;
	Ref<ImageTexture> m_DepthAtlasPrev;

	// Probe position SSBO
	Ref<StorageBuffer> m_ProbeRayDataSSBO;

	// Grid state
	glm::ivec3 m_ScrollOffset = glm::ivec3(0);
	glm::ivec3 m_LastGridSize = glm::ivec3(8, 4, 8);
	int m_TotalProbes = 0;
	int m_ProbesPerRow = 0;
	int m_CurrentProbeOffset = 0;
	unsigned int m_FrameIdx = 0;
		Ptr<Shader> m_ProbeVisShader;
		Ptr<VertexArray> m_ProbeVisVA;
		Ptr<VertexBuffer> m_ProbeVisVB;

		void BuildProbeVisGeometry()
		{
			// Single point at origin — translated per-probe via MVP matrix
			float point[] = { 0.0f, 0.0f, 0.0f };
			m_ProbeVisVA = CreatePtr<VertexArray>(1);
			m_ProbeVisVB = CreatePtr<VertexBuffer>(point, sizeof(point));
			VertexBufferLayout layout;
			layout.Push<float>(3);
			m_ProbeVisVA->AddBuffer(*m_ProbeVisVB, layout);
		}
};
class  DeferredLightingPass : public RenderPass
{
public:
	int DebugMode = 0;
	bool DDGIEnabled = true;

	void SetDDGIPass(DDGIPass* ddgi) { m_DDGIPass = ddgi; }

	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();

		float quadVertices[] = {
			-1.0f,  1.0f,  0.0f, 1.0f,
			-1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 1.0f
		};
		unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };

		m_QuadVA = CreatePtr<VertexArray>(4);
		m_QuadVB = CreatePtr<VertexBuffer>(quadVertices, sizeof(quadVertices));
		m_QuadIB = CreatePtr<IndexBuffer>(quadIndices, 6);
		VertexBufferLayout quadLayout;
		quadLayout.Push<float>(2);
		quadLayout.Push<float>(2);
		m_QuadVA->AddBuffer(*m_QuadVB, quadLayout);

		m_Shader = ShaderLibiray::Get("D:/Code/C++/Tsundere/res/shaders/DeferredLighting.shader");

		unsigned char white[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &m_DefaultWhiteTex);
		glBindTexture(GL_TEXTURE_2D, m_DefaultWhiteTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// Default black texture for DDGI fallback
		unsigned char black2[4] = { 0, 0, 0, 255 };
		glGenTextures(1, &m_DefaultBlackTex);
		glBindTexture(GL_TEXTURE_2D, m_DefaultBlackTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black2);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		unsigned char black[4] = { 0, 0, 0, 255 };
		glGenTextures(1, &m_DefaultCubemap);
		glBindTexture(GL_TEXTURE_CUBE_MAP, m_DefaultCubemap);
		for (int face = 0; face < 6; face++)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		CreateOutputTex(m_Spec.Width, m_Spec.Height);
	}

	void Execute(Ref<Scene> scene, RenderResources& resources) override
	{
		if (!resources.GBufferPosition)
			return;

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

		glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
		glViewport(0, 0, m_Spec.Width, m_Spec.Height);
		glDisable(GL_DEPTH_TEST);

		m_Shader->Bind();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, resources.GBufferPosition);
		m_Shader->SetUniform1i("u_GBufferPosition", 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, resources.GBufferNormal);
		m_Shader->SetUniform1i("u_GBufferNormal", 1);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, resources.GBufferAlbedo);
		m_Shader->SetUniform1i("u_GBufferAlbedo", 2);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, resources.GBufferSpecular);
		m_Shader->SetUniform1i("u_GBufferSpecular", 3);

		glActiveTexture(GL_TEXTURE4);
		if (resources.ShadowMask)
			glBindTexture(GL_TEXTURE_2D, resources.ShadowMask);
		else
			glBindTexture(GL_TEXTURE_2D, m_DefaultWhiteTex);
		m_Shader->SetUniform1i("u_ShadowMask", 4);

		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, resources.DepthTexture);
		m_Shader->SetUniform1i("u_Depth", 5);

		m_Shader->SetUniformVec3("u_LightDir", lightDir);
		m_Shader->SetUniformVec3("u_LightColor", lightColor);
		m_Shader->SetUniform1f("u_AmbientStrength", ambientStrength);
		m_Shader->SetUniformVec3("u_ViewPos", viewPos);

		mat4 viewNoTrans = mat4(mat3(currentcamera->GetViewFront()));
		mat4 proj = currentcamera->GetProj();
		mat4 invViewProjNoTrans = inverse(proj * viewNoTrans);
		m_Shader->SetUniformMat4f("u_InvViewProjNoTrans", invViewProjNoTrans);

		glActiveTexture(GL_TEXTURE6);
		if (currentcamera->skybox && currentcamera->skybox->m_Cmp)
			glBindTexture(GL_TEXTURE_CUBE_MAP, currentcamera->skybox->m_Cmp->GetMap());
		else
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_DefaultCubemap);
		m_Shader->SetUniform1i("u_Skybox", 6);

		// --- DDGI indirect diffuse ---
		glActiveTexture(GL_TEXTURE7);
		if (m_DDGIPass && DDGIEnabled && resources.DDGIIrradianceAtlas)
		{
			glBindTexture(GL_TEXTURE_2D, resources.DDGIIrradianceAtlas);
			m_Shader->SetUniform1i("u_DDGIEnabled", 1);
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, m_DefaultBlackTex);
			m_Shader->SetUniform1i("u_DDGIEnabled", 0);
		}
		m_Shader->SetUniform1i("u_DDGIIrradiance", 7);

		glActiveTexture(GL_TEXTURE8);
		if (m_DDGIPass && DDGIEnabled && resources.DDGIDepthAtlas)
			glBindTexture(GL_TEXTURE_2D, resources.DDGIDepthAtlas);
		else
			glBindTexture(GL_TEXTURE_2D, m_DefaultWhiteTex);
		m_Shader->SetUniform1i("u_DDGIDepth", 8);

		// --- Shadow Map (texture unit 9) ---
		glActiveTexture(GL_TEXTURE9);
		if (resources.ShadowMapDepth)
		{
			glBindTexture(GL_TEXTURE_2D, resources.ShadowMapDepth);
			m_Shader->SetUniformMat4f("u_LightViewProj", resources.ShadowLightViewProj);
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, m_DefaultWhiteTex);
			m_Shader->SetUniformMat4f("u_LightViewProj", glm::mat4(1.0f));
		}
		m_Shader->SetUniform1i("u_ShadowMap", 9);

		// DDGI grid parameters
		if (m_DDGIPass && DDGIEnabled)
		{
			const glm::ivec3& gs = m_DDGIPass->GetGridSize();
			m_Shader->SetUniform1i("u_DDGIGridSizeX", gs.x);
			m_Shader->SetUniform1i("u_DDGIGridSizeY", gs.y);
			m_Shader->SetUniform1i("u_DDGIGridSizeZ", gs.z);
			m_Shader->SetUniformVec3("u_DDGIGridOrigin", m_DDGIPass->GetGridOrigin());
			m_Shader->SetUniform1f("u_DDGISpacing", m_DDGIPass->GetSpacing());
			m_Shader->SetUniform1f("u_DDGIProbeRadius", m_DDGIPass->GetProbeRadius());
			m_Shader->SetUniform1f("u_DDGIDepthSharpness", m_DDGIPass->GetDepthSharpness());
			m_Shader->SetUniform1i("u_DDGIProbesPerRow", m_DDGIPass->GetProbesPerRow());
		}
		else
		{
			m_Shader->SetUniform1i("u_DDGIGridSizeX", 0);
		}

		m_Shader->SetUniform1f("u_Near", 0.1f);
		m_Shader->SetUniform1f("u_Far", 100.0f);
		m_Shader->SetUniform1i("u_DebugMode", DebugMode);

		Renderer renderer;
		renderer.DrawElement(*m_QuadVA, *m_QuadIB, *m_Shader);

			// --- DDGI probe debug overlay (rendered into the lighting output) ---
			if (m_DDGIPass && m_DDGIPass->ShowProbes)
			{
				mat4 view = currentcamera->GetViewFront();
				mat4 proj = currentcamera->GetProj();
				m_DDGIPass->RenderProbeDebug(view, proj);
			}

		glEnable(GL_DEPTH_TEST);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		resources.SceneColorTexture = m_OutputTex;
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
		CreateOutputTex(w, h);
	}

private:
	void CreateOutputTex(unsigned int w, unsigned int h)
	{
		if (m_OutputTex)
			glDeleteTextures(1, &m_OutputTex);
		if (m_OutputFBO)
			glDeleteFramebuffers(1, &m_OutputFBO);

		glGenTextures(1, &m_OutputTex);
		glBindTexture(GL_TEXTURE_2D, m_OutputTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenFramebuffers(1, &m_OutputFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, m_OutputFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutputTex, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	FrameBufferSpecification m_Spec;
	Ref<RHIShader> m_Shader;
	Ptr<VertexArray> m_QuadVA;
	Ptr<VertexBuffer> m_QuadVB;
	Ptr<IndexBuffer> m_QuadIB;
	unsigned int m_OutputTex = 0;
	unsigned int m_OutputFBO = 0;
	unsigned int m_DefaultWhiteTex = 0;
	unsigned int m_DefaultBlackTex = 0;
	unsigned int m_DefaultCubemap = 0;
	DDGIPass* m_DDGIPass = nullptr;
};