#pragma once
#include "Core/Layer/Layer.h"
#include "Core/Application.h"
#include "Core/MaterialLibrary.h"

// Asset editor for one .tmat. Call Open() with the file path; the layer is
// created the first time a material is shown and removed when the window closes.
class MaterialEditorPanel : public Engine::Layer
{
public:
	MaterialEditorPanel()
		: Layer("Material Editor")
	{
		Instance() = this;
	}

	~MaterialEditorPanel() override
	{
		if (Instance() == this)
			Instance() = nullptr;
	}

	static void Open(const std::string& path)
	{
		if (path.empty())
			return;
		Ref<Engine::Material> mat = Engine::MaterialLibrary::Load(path);
		if (!mat)
			return;
		if (!Instance())
			Engine::Application::Get().PushLayer(new MaterialEditorPanel());
		if (Instance())
			Instance()->Show(std::move(mat));
	}

	void OnImGuiRender() override;

private:
	void Show(Ref<Engine::Material> mat)
	{
		m_Material = std::move(mat);
		m_Open = true;
	}

	void DestroySelf()
	{
		Engine::Application::Get().PopLayer(this);
		delete this;
	}

	// Defined in the DLL. A header inline static would be a second copy in SandBox.
	T_API static MaterialEditorPanel*& Instance();

	Ref<Engine::Material> m_Material;
	bool m_Open = false;
};
