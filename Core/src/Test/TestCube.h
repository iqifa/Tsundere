#pragma once

#include "Test.h"
#include"GLHead.h"

#include"Debug/Debug.h"
#include"Trans/SceneCamera.h"
#include"Scene/Modle.h"
using namespace glm;

class SceneCamera;
namespace test
{
	class TestCube :
		public Test
	{
	public:
		void OnUpdate(float deltaTime) override;
		void OnRender() override;
		void OnImGuiRender() override;

		TestCube(float screenWidth, float screenHeight);
		~TestCube();
	private:
		unique_ptr<Shader>m_Shader;
		unique_ptr<VertexArray>m_vao;
		unique_ptr<VertexBuffer>m_VertexBuffer;
		unique_ptr<Texture>m_Texture;
		unique_ptr<Shader>ModleShader;
		unique_ptr<Shader>SingleShader;

		mat4 proj, view, model, mvp;
		float screenWidth, screenHeight;
		float time;

		Model modle;
	};
}


