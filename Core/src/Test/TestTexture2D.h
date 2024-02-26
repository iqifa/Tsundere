#pragma once

#include"ExternalFiles.h"
#include"GLHead.h"
#include"Test.h"
using namespace glm;
namespace test
{
	class TestTexture2D :public Test
	{
	public:
		TestTexture2D();
		~TestTexture2D();

		void OnUpdate(float deltaTime) override;
		void OnRender() override;
		void OnImGuiRender() override;

	private:
		vec3 m_translationA, m_translationB;
		mat4 proj, view;


		unique_ptr<VertexArray>m_vao;
		unique_ptr<IndexBuffer>m_InderBuffer;
		unique_ptr<Shader>m_Shader;
		unique_ptr<Texture>m_Texture;
		unique_ptr<VertexBuffer>m_VertexBuffer;
		/*VertexArray m_vao;
		IndexBuffer m_InderBuffer;
		Shader m_Shader;
		Texture m_Texture;
		VertexBuffer m_VertexBuffer;*/
	};
}