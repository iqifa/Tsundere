#include<GLHead.h>
#include<HeadLine.h>
#include<Scene/Scene.h>
#include<Scene/Mesh.h>
#include<Scene/BVHBuilder.h>
#include<Panels/MeshFilePath.h>
#include<Panels/Material.h>
#include<set>
#include<Debug/Debug.h>
class Scene;
class  FrameBuffer;


struct  RenderResources
{
	unsigned int SceneColorTexture = 0; // 上一阶段输出的场景颜色
	unsigned int VelocityTexture = 0;   // 上一阶段输出的运动矢量缓存 (Motion Vectors)
	unsigned int DepthTexture = 0;      // 深度图
	unsigned int ShadowMask = 0;        // 阴影遮罩纹理 (R8)

	// 渲染目标尺寸
	unsigned int SourceFBO = 0;        // 几何 Pass 的主 FBO（用于深度拷贝）
};

class  RenderPass
{
public:
	virtual ~RenderPass() = default;

	virtual void Init(Ref<FrameBuffer>& m_GBuffer) {}

	virtual void Execute(Ref<Scene> scene, RenderResources& resources) = 0;
};

class  GeometryPass : public RenderPass
{
	Ref<FrameBuffer> m_GBuffer;

	int m_FrameCount = 0;
	unsigned int m_VelocityAttachment;
	unsigned int m_DefaultTex = 0;
	mat4 m_PrevViewProjMatrix = mat4(1.0f);

	Ptr<VertexArray>va;
	Ptr<VertexBuffer>vb;
	Ptr<IndexBuffer>ibo;
	Ptr<Shader>shader;


public:
	bool EnableJitter = true;

	void Execute(Ref<Scene> scene, RenderResources& resources) override {
		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 currentViewProj = proj * view;

		if (EnableJitter)
			proj = Jittering(proj, m_GBuffer->GetSpecification().Width, m_GBuffer->GetSpecification().Height);

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

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_DefaultTex);

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

				mat->Render();

				mat->shader->SetUniformMat4f("MVP_matrix", proj * view * modelMat);
				mat->shader->SetUniformMat4f("model", modelMat);
				mat->shader->SetUniformMat4f("prevModel", modelMat);
				mat->shader->SetUniformMat4f("viewProj", currentViewProj);
				mat->shader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);
				mat->shader->SetUniformVec3("lightDir", lightDir);
				mat->shader->SetUniformVec3("lightColor", lightColor);
				mat->shader->SetUniform1f("ambientStrength", ambientStrength);
				mat->shader->SetUniformVec3("viewPos", viewPos);
				mat->shader->SetUniform1i("hasNormalMap", 0);

				renderer.DrawElement(*mesh.vao, *mesh.ibo, *(mat->shader));
				drewSomething = true;
			}
		}

		if (!drewSomething)
		{
			shader->Bind();
			shader->SetUniformMat4f("viewProj", currentViewProj);
			shader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);
			shader->SetUniformVec3("lightDir", lightDir);
			shader->SetUniformVec3("lightColor", lightColor);
			shader->SetUniform1f("ambientStrength", ambientStrength);
			shader->SetUniformVec3("viewPos", viewPos);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_DefaultTex);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, m_DefaultTex);

			mat4 model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
			mat4 mvp = proj * view * model;
			shader->SetUniformMat4f("MVP_matrix", mvp);
			shader->SetUniformMat4f("model", model);
			shader->SetUniformMat4f("prevModel", model);
			shader->SetUniform1i("hasNormalMap", 0);
			renderer.DrawElement(*va, *ibo, *(this->shader));
		}

		m_PrevViewProjMatrix = currentViewProj;
		m_FrameCount++;
		resources.SceneColorTexture = m_GBuffer->GetClolorAttachmentRenderID();
		resources.VelocityTexture = m_VelocityAttachment;
		resources.DepthTexture = m_GBuffer->GetDepthAttachmentRenderID();
	}

	void Init(Ref<FrameBuffer>& m_GBuffer)override {
		this->m_GBuffer = m_GBuffer;

		m_GBuffer->Bind();
		glGenTextures(1, &m_VelocityAttachment);

		glBindTexture(GL_TEXTURE_2D, m_VelocityAttachment);

		//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_GBuffer->GetSpecification().Width, m_GBuffer->GetSpecification().Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, m_GBuffer->GetSpecification().Width, m_GBuffer->GetSpecification().Height, 0, GL_RG, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_VelocityAttachment, 0);

		GLenum drawBufs[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		glDrawBuffers(2, drawBufs);

		m_GBuffer->UnBind();


		float position[] =
		{
			// Front face  (z=-0.5) normal(0,0,-1) tangent(1,0,0) bitangent(0,1,0)
			-0.5f,-0.5f,-0.5f,  0,0,-1,  0,0,  1,0,0,  0,1,0,
			 0.5f,-0.5f,-0.5f,  0,0,-1,  1,0,  1,0,0,  0,1,0,
			 0.5f, 0.5f,-0.5f,  0,0,-1,  1,1,  1,0,0,  0,1,0,
			 0.5f, 0.5f,-0.5f,  0,0,-1,  1,1,  1,0,0,  0,1,0,
			-0.5f, 0.5f,-0.5f,  0,0,-1,  0,1,  1,0,0,  0,1,0,
			-0.5f,-0.5f,-0.5f,  0,0,-1,  0,0,  1,0,0,  0,1,0,
			// Back face   (z= 0.5) normal(0,0,1) tangent(-1,0,0) bitangent(0,1,0)
			 0.5f,-0.5f, 0.5f,  0,0,1,  0,0,  -1,0,0,  0,1,0,
			-0.5f,-0.5f, 0.5f,  0,0,1,  1,0,  -1,0,0,  0,1,0,
			-0.5f, 0.5f, 0.5f,  0,0,1,  1,1,  -1,0,0,  0,1,0,
			-0.5f, 0.5f, 0.5f,  0,0,1,  1,1,  -1,0,0,  0,1,0,
			 0.5f, 0.5f, 0.5f,  0,0,1,  0,1,  -1,0,0,  0,1,0,
			 0.5f,-0.5f, 0.5f,  0,0,1,  0,0,  -1,0,0,  0,1,0,
			 // Left face   (x=-0.5) normal(-1,0,0) tangent(0,0,1) bitangent(0,1,0)
			 -0.5f,-0.5f, 0.5f,  -1,0,0,  0,0,  0,0,1,  0,1,0,
			 -0.5f,-0.5f,-0.5f,  -1,0,0,  1,0,  0,0,1,  0,1,0,
			 -0.5f, 0.5f,-0.5f,  -1,0,0,  1,1,  0,0,1,  0,1,0,
			 -0.5f, 0.5f,-0.5f,  -1,0,0,  1,1,  0,0,1,  0,1,0,
			 -0.5f, 0.5f, 0.5f,  -1,0,0,  0,1,  0,0,1,  0,1,0,
			 -0.5f,-0.5f, 0.5f,  -1,0,0,  0,0,  0,0,1,  0,1,0,
			 // Right face  (x= 0.5) normal(1,0,0) tangent(0,0,-1) bitangent(0,1,0)
			  0.5f,-0.5f,-0.5f,  1,0,0,  0,0,  0,0,-1,  0,1,0,
			  0.5f,-0.5f, 0.5f,  1,0,0,  1,0,  0,0,-1,  0,1,0,
			  0.5f, 0.5f, 0.5f,  1,0,0,  1,1,  0,0,-1,  0,1,0,
			  0.5f, 0.5f, 0.5f,  1,0,0,  1,1,  0,0,-1,  0,1,0,
			  0.5f, 0.5f,-0.5f,  1,0,0,  0,1,  0,0,-1,  0,1,0,
			  0.5f,-0.5f,-0.5f,  1,0,0,  0,0,  0,0,-1,  0,1,0,
			  // Top face    (y= 0.5) normal(0,1,0) tangent(1,0,0) bitangent(0,0,-1)
			  -0.5f, 0.5f,-0.5f,  0,1,0,  0,0,  1,0,0,  0,0,-1,
			   0.5f, 0.5f,-0.5f,  0,1,0,  1,0,  1,0,0,  0,0,-1,
			   0.5f, 0.5f, 0.5f,  0,1,0,  1,1,  1,0,0,  0,0,-1,
			   0.5f, 0.5f, 0.5f,  0,1,0,  1,1,  1,0,0,  0,0,-1,
			  -0.5f, 0.5f, 0.5f,  0,1,0,  0,1,  1,0,0,  0,0,-1,
			  -0.5f, 0.5f,-0.5f,  0,1,0,  0,0,  1,0,0,  0,0,-1,
			  // Bottom face (y=-0.5) normal(0,-1,0) tangent(1,0,0) bitangent(0,0,1)
			  -0.5f,-0.5f, 0.5f,  0,-1,0,  0,0,  1,0,0,  0,0,1,
			   0.5f,-0.5f, 0.5f,  0,-1,0,  1,0,  1,0,0,  0,0,1,
			   0.5f,-0.5f,-0.5f,  0,-1,0,  1,1,  1,0,0,  0,0,1,
			   0.5f,-0.5f,-0.5f,  0,-1,0,  1,1,  1,0,0,  0,0,1,
			  -0.5f,-0.5f,-0.5f,  0,-1,0,  0,1,  1,0,0,  0,0,1,
			  -0.5f,-0.5f, 0.5f,  0,-1,0,  0,0,  1,0,0,  0,0,1
		};

		unsigned int indices[] = {
			0,  1,  2,  3,  4,  5,
			6,  7,  8,  9,  10, 11,
			12, 13, 14, 15, 16, 17,
			18, 19, 20, 21, 22, 23,
			24, 25, 26, 27, 28, 29,
			30, 31, 32, 33, 34, 35
		};

		va = CreatePtr<VertexArray>(36);

		vb = CreatePtr<VertexBuffer>(position, 36 * 14 * sizeof(float));
		ibo = CreatePtr<IndexBuffer>(indices, 36);
		VertexBufferLayout layout;
		layout.Push<float>(3); // position
		layout.Push<float>(3); // normal
		layout.Push<float>(2); // texcoord
		layout.Push<float>(3); // tangent
		layout.Push<float>(3); // bitangent
		va->AddBuffer(*vb, layout);

		shader = CreatePtr<Shader>("D:/Code/C++/Tsundere/res/shaders/Lit.shader");

		unsigned char white[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &m_DefaultTex);
		glBindTexture(GL_TEXTURE_2D, m_DefaultTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}

	void OnFboResize(unsigned int width, unsigned int height)
	{
		glDeleteTextures(1, &m_VelocityAttachment);
		glGenTextures(1, &m_VelocityAttachment);
		glBindTexture(GL_TEXTURE_2D, m_VelocityAttachment);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, width, height, 0, GL_RG, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		m_GBuffer->Bind();
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_VelocityAttachment, 0);
		GLenum drawBufs[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		glDrawBuffers(2, drawBufs);
		m_GBuffer->UnBind();
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


class TAAPass : public RenderPass
{
public:
	bool Enabled = true;

	void Init(Ref<FrameBuffer>& fb) override
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

class ShadowPass : public RenderPass
{
public:
	bool Enabled = true;
	float LightDistance = 50.0f;

	void Init(Ref<FrameBuffer>& fb) override
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
	void Init(Ref<FrameBuffer>& fb) override
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

	void Init(Ref<FrameBuffer>& fb) override
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
