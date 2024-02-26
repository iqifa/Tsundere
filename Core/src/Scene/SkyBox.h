#pragma once
#ifndef SKYBOX
#define SKYBOX

#include"GLHead.h"
#include"HeadLine.h"
#include"CubeMap.h"

	struct SkyBox
	{
		vector<string> texpaths;

		unique_ptr<CubeMap>m_Cmp;
		unique_ptr<Shader>m_Shader;
		unique_ptr<VertexArray>m_vao;
		unique_ptr<VertexBuffer>m_VertexBuffer;


		SkyBox() = default;
		SkyBox(vector<string> vec) :texpaths(vec)
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
			m_Cmp = make_unique<CubeMap>(texpaths);
			m_Cmp->Bind();

			m_Shader = make_unique<Shader>("res/shaders/SkyBox.shader");
			m_Shader->Bind();

			m_vao = make_unique<VertexArray>(36);
			m_VertexBuffer = make_unique<VertexBuffer>(skyboxVertices, 36 * 5 * sizeof(float));

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
