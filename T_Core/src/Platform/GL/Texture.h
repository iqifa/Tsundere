#pragma once
#include"Renderer.h"
#include"Debug/Debug.h"
#include"HeadLine.h"
#include<shared_mutex>
class T_API Texture
{
private:
	unsigned int m_RendererID;
	std::string m_FilePath;
	unsigned char* m_LocalBuffer;
	int m_Width, m_Height, m_BPP;

public:
	Texture(const std::string& path);
	~Texture();
	Texture() : m_RendererID(0), m_LocalBuffer(nullptr), m_Width(0), m_Height(0), m_BPP(0) {}

	void Bind(unsigned int slot=0) const;
	void UnBind() const;

	unsigned int GetTextureID();

	inline std::string GetPath()const { return m_FilePath; }
	inline int GetWidth()const { return m_Width; }
	inline int GetHeight()const { return m_Height; }

	// Create from pre-loaded pixel data (for async loading).
	// Takes ownership of pixelData and frees it after GPU upload.
	static Ref<Texture> CreateFromPixels(const std::string& path,
		unsigned char* pixelData, int width, int height, int channels);
};
class T_API TextureLibiary {
public:
	static std::unordered_map<std::string, Ref<Texture>> m_TextureMap;
	static std::shared_mutex s_Mutex;

	static void Add(Ref<Texture>tex)
	{
		std::unique_lock lock(s_Mutex);
		auto& path = tex->GetPath();
		m_TextureMap[path] = tex;
	}

	static Ref<Texture> Load(const std::string& filepath)
	{
		{
			std::shared_lock lock(s_Mutex);
			auto it = m_TextureMap.find(filepath);
			if (it != m_TextureMap.end())
			{
				Warn_Core("Texture:" + filepath + "has Load")
				return it->second;
			}
		}
		auto tex = CreateRef<Texture>(filepath);
		Add(tex);
		return tex;
	}

	static Ref<Texture> Get(const std::string& filepath)
	{
		{
			std::shared_lock lock(s_Mutex);
			auto it = m_TextureMap.find(filepath);
			if (it != m_TextureMap.end())
				return it->second;
		}
		return Load(filepath);
	}
};
