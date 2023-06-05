#include"NewTest.h"

test::NewTest::NewTest():model_mat(mat4(1.0f)), proj(mat4(1.0f)), view(mat4(1.0f))
{
	shader=CreatePtr<Shader>("res/shaders/default.shdaer");
	model =CreatePtr<Model>("res/defaultmodle/default.obj");

}

void test::NewTest::OnRender()
{
	
}