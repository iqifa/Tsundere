#include<GLHead.h>
#include<HeadLine.h>
#include<Scene/Scene.h>
#include<Scene/Mesh.h>
class Scene;
class  FrameBuffer;


struct  RenderResources
{
	unsigned int SceneColorTexture = 0; // 上一阶段输出的场景颜色
	unsigned int VelocityTexture = 0;   // 上一阶段输出的运动矢量缓存 (Motion Vectors)
	unsigned int DepthTexture = 0;      // 深度图

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
			if (meshrender.materials.empty())
				continue;

			mat4 model = transform.GetTransform();
			mat4 mvp = proj * view * model;

			Ref<Material> mat = meshrender.materials[0];

			mat->Render();
			//mat->shader->SetUniformMat4f("MVP_matrix", mvp);
			//mat->shader->SetUniformMat4f("model", model);
			//mat->shader->SetUniformMat4f("prevModel", model);
			//mat->shader->SetUniformMat4f("viewProj", currentViewProj);
			//mat->shader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);

			//mat->shader->SetUniformVec3("lightDir", lightDir);
			//mat->shader->SetUniformVec3("lightColor", lightColor);
			//mat->shader->SetUniform1f("ambientStrength", ambientStrength);
			//mat->shader->SetUniformVec3("viewPos", viewPos);
			//mat->shader->SetUniform1i("hasNormalMap", 0);

			mat->shader->SetUniformMat4f("viewProj", currentViewProj);
			mat->shader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);
			mat->shader->SetUniformVec3("lightDir", lightDir);
			//mat->shader->SetUniformVec3("lightColor", lightColor);
			mat->shader->SetUniform1f("ambientStrength", ambientStrength);
			mat->shader->SetUniformVec3("viewPos", viewPos);

			renderer.DrawElement(*va, *ibo, *(mat->shader));
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
		glGenTextures(1, &m_VelocityAttachment);

		glBindTexture(GL_TEXTURE_2D, m_VelocityAttachment);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_GBuffer->GetSpecification().Width, m_GBuffer->GetSpecification().Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);


		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_VelocityAttachment, 0);


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
