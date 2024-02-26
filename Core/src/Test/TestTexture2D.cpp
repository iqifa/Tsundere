#pragma once
#include"TestTexture2D.h"


namespace test {
	TestTexture2D::TestTexture2D() :m_translationA(200, 200, 0), m_translationB(400, 200, 0)
	{

		float position[] = {
		   100.0f, 100.0f,0.0f,0.0f,
		   200.0f, 100.0f,1.0f,0.0f,
		   200.0f, 200.0f,1.0f,1.0f,
		   100.0f, 200.0f,0.0f,1.0f
		};

		unsigned int indices[] = {
			0,1,2,
			2,3,0
		};

		m_Shader = make_unique<Shader>("res/shaders/Basic.shader");
		//m_Shader = Shader("res/shaders/Basic.shader");
		m_vao = make_unique<VertexArray>();
		//m_vao = VertexArray();
		m_InderBuffer = make_unique<IndexBuffer>(indices, 6);
		//m_InderBuffer = IndexBuffer(indices, 6);

		m_VertexBuffer=make_unique<VertexBuffer>(position, 4 * 4 * sizeof(float));
		//m_VertexBuffer = VertexBuffer(position, 4 * 4 * sizeof(float));
		VertexBufferLayout layout;
		layout.Push<float>(2);
		layout.Push<float>(2);
		m_vao->AddBuffer(*m_VertexBuffer, layout);


		proj = ortho(0.0f, 1080.0f, 0.0f, 960.0f, -1.0f, 1.0f);
		view = translate(mat4(1.0f), vec3(-100, 0, 0));


		m_Shader->Bind();
		m_Shader->SetUniform4f("u_Color", 1.0f, 0.3f, 0.8f, 1.0f);
		m_Texture=make_unique<Texture>("res/texture/Sekiro.jpg");
		//m_Texture = Texture("res/texture/Sekiro.jpg");
		m_Texture->Bind(0);
		m_Shader->SetUniform1i("u_Texture", 0);
	}
	TestTexture2D::~TestTexture2D()
	{
	}
	void TestTexture2D::OnUpdate(float deltaTime)
	{
	}
	void TestTexture2D::OnRender()
	{
		//glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		Renderer renderer;
		{
			mat4 model = translate(mat4(1.0f), m_translationA);
			mat4 mvp = proj * view * model;
			m_Shader->Bind();
			m_Shader->SetUniformMat4f("u_MVP", mvp);
			renderer.DrawElement(*m_vao, *m_InderBuffer, *m_Shader);
		}
		{
			mat4 model = translate(mat4(1.0f), m_translationB);
			mat4 mvp = proj * view * model;
			m_Shader->Bind();
			m_Shader->SetUniformMat4f("u_MVP", mvp);
			renderer.DrawElement(*m_vao, *m_InderBuffer, *m_Shader);
		}
	}
	void TestTexture2D::OnImGuiRender()
	{
		ImGui::SliderFloat3("Translation A", &m_translationA.x, 0.0f, 1080.f);
		ImGui::SliderFloat3("Translation B", &m_translationB.x, 0.0f, 1080.f);
		ImGui::Text("Application average %.3f ms/frame(%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	}
}