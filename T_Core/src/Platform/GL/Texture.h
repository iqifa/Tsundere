#pragma once
#include"Renderer.h"
#include"Debug/Debug.h"
#include"HeadLine.h"
class Texture
{
private:
	unsigned int m_RendererID;
	string m_FilePath;
	unsigned char* m_LocalBuffer;
	int m_Width, m_Height, m_BPP;

public:
	Texture(const string& path);
	~Texture();
	Texture() {}

	void Bind(unsigned int slot=0) const;
	void UnBind() const;

	unsigned int GetTextureID();

	inline string GetPath()const { return m_FilePath; }
	inline int GetWidth()const { return m_Width; }
	inline int GetHeight()const { return m_Height; }
};
class TextureLibiary {
public:
	static unordered_map<string, Ref<Texture>> m_TextureMap;
	static void Add(Ref<Texture>tex)
	{
		auto& path = tex->GetPath();
		m_TextureMap[path] = tex;
	}
	 
	 
	/// <summary>
	/// 感觉有优化的空间
	/// </summary>
	/// <param name="filepath"></param>
	/// <returns></returns>
	static Ref<Texture> Load(const string& filepath)
	{
		if (m_TextureMap.find(filepath) != m_TextureMap.end())
		{
			debugwarring("Texture:" + filepath + "has Load")
				return m_TextureMap[filepath];
		}
		else {
			auto& tex = CreateRef<Texture>(filepath);
			Add(tex);
			return tex;
		}
	}
	static Ref<Texture> Get(const string& filepath)
	{
		if (m_TextureMap.find(filepath) != m_TextureMap.end())
		{
			auto& tex = m_TextureMap[filepath];
			return tex;
		}
		else
		{
			return Load(filepath);
		}
	}
};
