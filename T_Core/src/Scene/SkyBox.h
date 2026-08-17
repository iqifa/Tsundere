#pragma once
#ifndef SKYBOX
#define SKYBOX

#include"GLHead.h"
#include"HeadLine.h"

	struct SkyBox
	{
		std::vector<std::string> texpaths;

		Ref<RHITextureCube>m_Cmp;
		std::unique_ptr<GLShader>m_Shader;
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
			m_Cmp = RHITextureCube::Create(TextureCubeDesc{ 0, Format::RGBA8_UNORM, texpaths, FilterMode::Linear, FilterMode::Linear });
			m_Cmp->Bind(0);

			m_Shader = std::make_unique<GLShader>("D:\\Code\\C++\\Tsundere\\res/shaders/SkyBox.shader");
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
			glBindTexture(GL_TEXTURE_CUBE_MAP, (unsigned int)m_Cmp->GetNativeID());
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
