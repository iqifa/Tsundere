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
    unsigned int Width = 1080;
    unsigned int Height = 960;
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
	mat4 m_PrevViewProjMatrix = mat4(1.0f);

	Ptr<VertexArray>va;
	Ptr<VertexBuffer>vb;
	Ptr<IndexBuffer>ibo;
	Ptr<Shader>shader;


public:
    bool EnableJitter = true;

    void Execute(Ref<Scene> scene, RenderResources& resources) override {
		//m_GBuffer->Bind();
        //// ����ʵ�岢����
        //for (auto entityID : scene->m_Registry.view<entt::entity>())
        //{
        //    Entity entity{ scene.get(), entityID };
        //    if (/*entity.HasComponent<MeshFile>() && */entity.HasComponent<Material>()) {
        //        entity.Draw();
        //    }
        //}

		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		mat4 currentViewProj = proj * view;

		if (EnableJitter)
			proj = Jittering(proj, m_GBuffer->GetSpecification().Width, m_GBuffer->GetSpecification().Height);

		mat4 model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
		mat4 mvp_jittered = proj * view;

		shader->Bind();
		shader->SetUniformMat4f("MVP_matrix", mvp_jittered*model);
		shader->SetUniformMat4f("model", model);
		shader->SetUniformMat4f("prevModel", model);
		shader->SetUniformMat4f("viewProj", currentViewProj);
		shader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);




		shader->SetUniformVec3("print_color", vec3(0, .5, .3));

		Renderer renderer;
		renderer.DrawElement(*va, *ibo, *(this->shader));

		m_PrevViewProjMatrix = currentViewProj;
		m_FrameCount++;
        //m_GBuffer->UnBind();
        resources.SceneColorTexture = m_GBuffer->GetClolorAttachmentRenderID(); // 后续 Pass 支持直接读取
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
		-0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
		 0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
		-0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

		-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
		 0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
		-0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
		-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,

		-0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
		-0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
		-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
		-0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
		 0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
		 0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
		 0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
		 0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
		 0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
		 0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
		-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,

		-0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
		-0.5f,  0.5f,  0.5f,  0.0f, 0.0f,
		-0.5f,  0.5f, -0.5f,  0.0f, 1.0f
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

		vb = CreatePtr<VertexBuffer>(position, 36 * 5 * sizeof(float));
		ibo = CreatePtr<IndexBuffer>(indices, 36);
		VertexBufferLayout layout;
		layout.Push<float>(3);
		layout.Push<float>(2);
		va->AddBuffer(*vb, layout);

		shader = CreatePtr<Shader>("D:/Code/C++/Tsundere/res/shaders/Basic.shader");
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


// PostProcessPass.h
//class PostProcessPass : public RenderPass
//{
//protected:
//    Ref<VertexArray> m_QuadVA;
//    Ref<VertexBuffer> m_QuadVB;
//    Ref<IndexBuffer> m_QuadIB;
//public:
//    virtual void Init() override
//    {
//        float quadVertices[] = {
//            // pos         // tex
//            -1.0f,  1.0f,  0.0f, 1.0f,
//            -1.0f, -1.0f,  0.0f, 0.0f,
//             1.0f, -1.0f,  1.0f, 0.0f,
//             1.0f,  1.0f,  1.0f, 1.0f
//        };
//        unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };
//
//        m_QuadVA = CreatePtr<VertexArray>(4);
//        m_QuadVB = CreatePtr<VertexBuffer>(quadVertices, sizeof(quadVertices));
//        m_QuadIB = CreatePtr<IndexBuffer>(quadIndices, 6);
//
//        VertexBufferLayout quadLayout;
//        quadLayout.Push<float>(2); // pos
//        quadLayout.Push<float>(2); // tex
//        m_QuadVA->AddBuffer(*m_QuadVB, quadLayout);
//    }
//
//    
//    virtual void Execute(Ref<Scene> scene, RenderResources& resources, Ref<FrameBuffer> target_fbo)
//    {
//        target_fbo->Bind();
//        BindCustomShaderAndUniforms(input_texture);
//
//        Renderer renderer;
//        renderer.DrawElement(*m_QuadVA, *m_QuadIB, *GetCustomShader());
//        target_fbo->UnBind();
//    }
//
//protected:
//    virtual void BindCustomShaderAndUniforms(unsigned int input_texture) = 0;
//    virtual Ref<Shader> GetCustomShader() = 0;
//};
//
//class TAAPass : public PostProcessPass
//{
//private:
//    Ref<Shader> m_TAAShader;
//    Ref<FrameBuffer> m_HistoryFBO;
//
//protected:
//    void BindCustomShaderAndUniforms(unsigned int input_texture) override 
//    {
//        m_TAAShader->Bind();
//        
//        // �󶨵�ǰ֡
//        glActiveTexture(GL_TEXTURE0);
//        glBindTexture(GL_TEXTURE_2D, input_texture);
//        m_TAAShader->SetUniform1i("u_CurrentColor", 0);
//
//        // 绑定历史帧
//        glActiveTexture(GL_TEXTURE1);
//        glBindTexture(GL_TEXTURE_2D, m_HistoryFBO->GetClolorAttachmentRenderID());
//        m_TAAShader->SetUniform1i("u_HistoryColor", 1);
//        
//        // �� Velocity Buffer (��������һ��ȫ�ֵ� G-Buffer ���Է���)
//        // ...
//    }
//
//    Ref<Shader> GetCustomShader() override { return m_TAAShader; }
//};