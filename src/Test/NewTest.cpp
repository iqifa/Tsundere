#include"NewTest.h"

test::NewTest::NewTest()
{
	tex1 = make_unique<Texture>("res/texture/jinx.jpeg");
	tex2 = make_unique<Texture>("res/texture/Sekiro.jpg");
	//model = make_unique<Model>("res/TestCube/TestCube.obj");
	model = make_unique<Model>("res/modle/nanosuit.obj");
	//shader = make_unique<Shader>("res/shaders/Test.shader");
	shader = *new Shader("res/shaders/Test.shader");
}

void test::NewTest::OnRender()
{
	shader.Bind();
	mat4 view = currentcamera->GetViewFront();
	mat4 proj = currentcamera->GetProj();
	mat4 modle(1.0);
	mat4 mvp = proj * view * modle;
	shader.SetUniformMat4f("u_MVP", mvp);
	shader.SetUniform1i("texture_diffuse1", 0);
	model->Draw(shader);
}