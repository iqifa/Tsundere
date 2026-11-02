#pragma once
#include "Core/Layer/Layer.h"
#include "Core/Application.h"
#include "Platform/RHI/RHITextureLibrary.h"

// Preview for one image file. Open() creates the layer the first time a
// texture is shown and removes it when the window closes.
class TextureViewerPanel : public Engine::Layer
{
public:
	TextureViewerPanel()
		: Layer("Texture")
	{
		Instance() = this;
	}

	~TextureViewerPanel() override
	{
		if (Instance() == this)
			Instance() = nullptr;
	}

	static void Open(const std::string& path)
	{
		if (path.empty())
			return;
		Ref<RHITexture2D> tex = TextureLibiary::Load(path);
		if (!tex)
			return;
		if (!Instance())
			Engine::Application::Get().PushLayer(new TextureViewerPanel());
		if (Instance())
			Instance()->Show(path, std::move(tex));
	}

	void OnImGuiRender() override;

private:
	void Show(const std::string& path, Ref<RHITexture2D> tex)
	{
		m_Path = path;
		m_Texture = std::move(tex);
		m_Open = true;
	}

	void DestroySelf()
	{
		Engine::Application::Get().PopLayer(this);
		delete this;
	}

	// Defined in the DLL. A header inline static would be a second copy in SandBox.
	T_API static TextureViewerPanel*& Instance();

	std::string m_Path;
	Ref<RHITexture2D> m_Texture;
	bool m_Open = false;
};
