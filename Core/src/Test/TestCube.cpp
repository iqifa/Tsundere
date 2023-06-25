#pragma once
#include"TestCube.h"
#include "Debug/Debug.h"
//#include"Scene/Entity.h"
void test::TestCube::OnUpdate(float deltaTime)
{
}

void test::TestCube::OnRender()
{
	//glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	//glClear(GL_COLOR_BUFFER_BIT);

	//SceneCamera::currentcamera->GLPrecessInput(currentwin, 0.05f);
	view = currentcamera->GetViewFront();
	proj = currentcamera->GetProj();
	model = scale(model, vec3(1.0f, 1.0f, 1.0f));
	mat4 modle2 = scale(model, vec3(1.1f, 1.1f, 1.1f));

	glClear(GL_STENCIL_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

	glStencilFunc(GL_ALWAYS, 1, 0xFF);
	glStencilMask(0xFF);

	mvp = proj * view * model;
	m_Shader->Bind();
	m_Shader->SetUniformMat4f("u_MVP", mvp);
	
	modle.Draw(*m_Shader);

	glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
	glStencilMask(0x00);
	glDisable(GL_DEPTH_TEST);
	
	mvp = proj * view * modle2;
	SingleShader->Bind();
	SingleShader->SetUniformMat4f("u_MVP", mvp);
	modle.Draw(*SingleShader);

	glStencilMask(0xFF);
	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glEnable(GL_DEPTH_TEST);
}

void test::TestCube::OnImGuiRender()
{
}

test::TestCube::TestCube(float screenWidth, float screenHeight)
	:screenWidth(screenWidth), screenHeight(screenHeight),
	model(mat4(1.0f)), proj( mat4(1.0f)),view(mat4(1.0f)),modle(Model("res/defaultmodle/default.obj"))/*,mesh(Mesh())*/
{
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


	m_Shader = make_unique<Shader>("res/shaders/default.shader", "default");
	m_Shader->Bind();
	SingleShader = CreatePtr<Shader>("res/shaders/Test.shader", "Test");

	m_vao = make_unique<VertexArray>(36);
	m_VertexBuffer = make_unique<VertexBuffer>(position, 36 * 5 * sizeof(float));

	m_Texture = make_unique<Texture>("res/texture/jinx.jpeg");
	m_Texture->Bind(1);

	VertexBufferLayout layout;
	layout.Push<float>(3);
	layout.Push<float>(2);
	m_vao->AddBuffer(*m_VertexBuffer, layout);

	//model = rotate(model, (float)glfwGetTime() * radians(50.0f), vec3(0.5f, 1.0f, 0.0f));
	view = translate(view, vec3(0.0f, 0.0f, -3.0f));
	proj = perspective(radians(45.0f), screenWidth / screenHeight, 0.1f, 100.0f);

	mvp = proj * view * model;
	m_Shader->SetUniformMat4f("u_MVP", mvp);

	////Model modle("res/modle/nanosuit.obj");
	ModleShader = make_unique<Shader>("res/shaders/Modle.shader");
	ModleShader->SetUniformMat4f("projection", proj);
	ModleShader->SetUniformMat4f("view", view);
	ModleShader->SetUniformMat4f("model", model);

	
}

test::TestCube::~TestCube()
{
	m_Shader.release();
}
