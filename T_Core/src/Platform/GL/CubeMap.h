#pragma once
#include"ExternalFiles.h"
#include"HeadLine.h"
#include"Debug/Debug.h"
class T_API CubeMap
{
public:
	CubeMap() = default;
	CubeMap(CubeMap& cubemap) = default;
	CubeMap(std::vector<std::string> TexFilePaths);
	~CubeMap();

	void Bind()const;
	void UnBind()const;

	inline unsigned int GetMap(){
		return m_RendererID;
	}
private:
	unsigned int m_RendererID;
	unsigned char* m_LocalBuffer;
	
	std::vector<std::string> textures_face;

	int m_Height, m_Width, m_Channels;
};

