#pragma once
#ifndef SKYBOX
#define SKYBOX

#include"GLHead.h"
#include"HeadLine.h"

	struct SkyBox
	{
		std::vector<std::string> texpaths;

		Ref<RHITextureCube>m_Cmp;
		Ref<RHIShader>m_Shader;
		Ref<RHIPipeline>m_Pipeline;
		Ref<RHIBuffer>m_VertexBuffer;


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

			m_Shader = RHIShader::Create("D:\\Code\\C++\\Tsundere\\res/shaders/SkyBox.shader");
			m_Shader->Bind();

			m_VertexBuffer = RHIBuffer::Create(BufferDesc{ (uint32_t)sizeof(skyboxVertices), BufferUsage::Vertex, false, skyboxVertices });

			VertexLayout layout;
			layout.stride = 3 * sizeof(float);
			layout.attributes = { { 0, VertexFormat::Float3, 0 } };
			PipelineDesc desc;
			desc.shader = m_Shader;
			desc.vertexLayout = layout;
			desc.cullMode = CullMode::None;
			desc.depthWrite = false;
			m_Pipeline = RHIPipeline::Create(desc);
			m_Pipeline->SetupVertexFormat(m_VertexBuffer);
		}
		void Bind()
		{
			m_Cmp->Bind(0);
		}
	};

#endif // !SKYBOX
