#pragma once
#include "Core/Layer/Layer.h"
#include "Core/Application.h"
#include "Platform/RHI/RHITextureLibrary.h"
#include "Core/Assets/ImportSettings.h"

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
	T_API void Show(const std::string& path, Ref<RHITexture2D> tex);
	void DrawImportSettings();

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
	// m_ImportSaved mirrors the .meta; m_ImportEdit is the unapplied UI copy.
	Engine::TextureImportSettings m_ImportSaved;
	Engine::TextureImportSettings m_ImportEdit;
	std::string m_ImportError;
};
