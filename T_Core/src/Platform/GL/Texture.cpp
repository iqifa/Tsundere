#include "Texture.h"
#include "stb_image/stb_image.h"
#include "Core/Assets/GPUDeletionQueue.h"
#include "Core/Assets/TextureAsset.h"
#include <shared_mutex>
using namespace std;
Texture::Texture(const string& path)
	:m_FilePath(path), m_LocalBuffer(nullptr), m_Height(0), m_Width(0)
{
	// OpenGL 原点在左下角，图片存储原点在左上角，需要翻转
	stbi_set_flip_vertically_on_load(1);
	m_LocalBuffer = stbi_load(path.c_str(), &m_Width, &m_Height, &m_BPP, 0);

	if (m_LocalBuffer)
	{
		GLenum format = GL_RGBA8;
		if (m_BPP == 1)
			format = GL_RED;
		else if (m_BPP == 3)
			format = GL_RGB;
		else if (m_BPP == 4)
			format = GL_RGBA;

		glGenTextures(1, &m_RendererID);
		glBindTexture(GL_TEXTURE_2D, m_RendererID);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

		//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_LocalBuffer);
		glTexImage2D(GL_TEXTURE_2D, 0, format, m_Width, m_Height, 0, format, GL_UNSIGNED_BYTE, m_LocalBuffer);
		glBindTexture(GL_TEXTURE_2D, 0);

		stbi_image_free(m_LocalBuffer);
	}
	else
	{
		Error_Core("Texture failed to load at path:"+path);
		stbi_image_free(m_LocalBuffer);
	}
	
}

Texture::~Texture()
{
	// 必须在 GL 主线程执行 glDeleteTextures。
	// 如果 shared_ptr 最后一个引用在非 GL 线程释放，
	// 把删除操作推入队列，主线程 Flush() 时统一执行。
	unsigned id = m_RendererID;
	if (id != 0) {
		GPUDeletionQueue::Enqueue([id]() {
			glDeleteTextures(1, &id);
		});
	}
}

void Texture::Bind(unsigned int slot) const
{
	glActiveTexture(GL_TEXTURE0+slot);
	glBindTexture(GL_TEXTURE_2D, m_RendererID);
}

void Texture::UnBind() const
{
	glBindTexture(GL_TEXTURE_2D, 0);
}

unsigned int Texture::GetTextureID()
{
	return this->m_RendererID;
}

unordered_map<string, Ref<Texture>> TextureLibiary::m_TextureMap;
shared_mutex TextureLibiary::s_Mutex;

Ref<Texture> Texture::CreateFromAsset(const TextureAsset& asset)
{
	if (!asset.IsValid()) return nullptr;

	Ref<Texture> tex = CreateRef<Texture>();
	tex->m_FilePath = asset.path;
	tex->m_Width    = asset.width;
	tex->m_Height   = asset.height;
	tex->m_BPP      = asset.channels;

	GLenum format = GL_RGBA;
	if (asset.channels == 1)      format = GL_RED;
	else if (asset.channels == 3) format = GL_RGB;
	else if (asset.channels == 4) format = GL_RGBA;

	glGenTextures(1, &tex->m_RendererID);
	glBindTexture(GL_TEXTURE_2D, tex->m_RendererID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, format, asset.width, asset.height,
		0, format, GL_UNSIGNED_BYTE, asset.pixels.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	// asset.pixels 生命周期由调用方管理，不在此释放
	return tex;
}

Ref<Texture> Texture::CreateFromPixels(const std::string& path,
	unsigned char* pixelData, int width, int height, int channels)
{
	Ref<Texture> tex = CreateRef<Texture>();
	tex->m_FilePath = path;
	tex->m_Width = width;
	tex->m_Height = height;
	tex->m_BPP = channels;

	GLenum format = GL_RGBA;
	if (channels == 1)      format = GL_RED;
	else if (channels == 3) format = GL_RGB;
	else if (channels == 4) format = GL_RGBA;

	glGenTextures(1, &tex->m_RendererID);
	glBindTexture(GL_TEXTURE_2D, tex->m_RendererID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glTexImage2D(GL_TEXTURE_2D, 0, format, width, height,
		0, format, GL_UNSIGNED_BYTE, pixelData);
	glBindTexture(GL_TEXTURE_2D, 0);

	stbi_image_free(pixelData);

	return tex;
}

