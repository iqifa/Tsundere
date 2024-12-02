#pragma once
#ifndef SKYBOX
#define SKYBOX

#include"GLHead.h"
#include"HeadLine.h"
#include"CubeMap.h"

	struct SkyBox
	{
		std::vector<std::string> texpaths;

		std::unique_ptr<CubeMap>m_Cmp;
		std::unique_ptr<Shader>m_Shader;
		std::unique_ptr<VertexArray>m_vao;
		std::unique_ptr<VertexBuffer>m_VertexBuffer;


		SkyBox() = default;
		SkyBox(std::vector<std::string> vec) :texpaths(vec)
		{
			float skyboxVertices[] = {
				// positions          
				-1.0f,  1.0f, -1.0f,
				-1.0f, -1.0f, -1.0f,
				 1.0f, -1.0f, -1.0f,
				 1.0f, -1.0f, -1.0f,
				 1.0f,  1.0f, -1.0f,
				-1.0f,  1.0f, -1.0f,

				-1.0f, -1.0f,  1.0f,
				-1.0f, -1.0f, -1.0f,
				-1.0f,  1.0f, -1.0f,
				-1.0f,  1.0f, -1.0f,
				-1.0f,  1.0f,  1.0f,
				-1.0f, -1.0f,  1.0f,

				 1.0f, -1.0f, -1.0f,
				 1.0f, -1.0f,  1.0f,
				 1.0f,  1.0f,  1.0f,
				 1.0f,  1.0f,  1.0f,
				 1.0f,  1.0f, -1.0f,
				 1.0f, -1.0f, -1.0f,

				-1.0f, -1.0f,  1.0f,
				-1.0f,  1.0f,  1.0f,
				 1.0f,  1.0f,  1.0f,
				 1.0f,  1.0f,  1.0f,
				 1.0f, -1.0f,  1.0f,
				-1.0f, -1.0f,  1.0f,

				-1.0f,  1.0f, -1.0f,
				 1.0f,  1.0f, -1.0f,
				 1.0f,  1.0f,  1.0f,
				 1.0f,  1.0f,  1.0f,
				-1.0f,  1.0f,  1.0f,
				-1.0f,  1.0f, -1.0f,

				-1.0f, -1.0f, -1.0f,
				-1.0f, -1.0f,  1.0f,
				 1.0f, -1.0f, -1.0f,
				 1.0f, -1.0f, -1.0f,
				-1.0f, -1.0f,  1.0f,
				 1.0f, -1.0f,  1.0f
			};
			m_Cmp = std::make_unique<CubeMap>(texpaths);
			m_Cmp->Bind();

			m_Shader = std::make_unique<Shader>("res/shaders/SkyBox.shader");
			m_Shader->Bind();

			m_vao = std::make_unique<VertexArray>(36);
			m_VertexBuffer = std::make_unique<VertexBuffer>(skyboxVertices, 36 * 5 * sizeof(float));

			VertexBufferLayout layout;
			layout.Push<float>(3);
			m_vao->AddBuffer(*m_VertexBuffer, layout);
		}
		void Bind()
		{
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Cmp->GetMap());
		}
		void UnBind()
		{
			glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
		}
		void Render()
		{

		}
	};

#endif // !SKYBOX
