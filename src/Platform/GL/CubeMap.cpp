#include"CubeMap.h"
#include"Renderer.h"
CubeMap::CubeMap(vector<string> TexFilePaths):textures_face(TexFilePaths),m_Height(0),m_Width(0),m_LocalBuffer(nullptr)
{
	////·´×ªÎÆÀí
	//stbi_set_flip_vertically_on_load(1);
	GLCall(glGenTextures(1, &m_RendererID));
	glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);

	for (unsigned int i = 0; i < textures_face.size(); i++)
	{
		m_LocalBuffer = stbi_load(textures_face[i].c_str(), &m_Height, &m_Width, &m_Channels, 4);
		if (m_LocalBuffer)
		{
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_LocalBuffer);
			stbi_image_free(m_LocalBuffer);
		}
		else
		{
			stbi_image_free(m_LocalBuffer);
			string errormsg = "CubeMap" + to_string(i) + "Can't Find\n" + "FilePath" + textures_face[i]+"\n";
			debugerror(errormsg);
		}
		
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

CubeMap::~CubeMap()
{
	glDeleteTextures(1, &m_RendererID);
}

void CubeMap::Bind() const
{ 
	glActiveTexture(GL_TEXTURE_CUBE_MAP);
	glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
}

void CubeMap::UnBind() const
{
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}
