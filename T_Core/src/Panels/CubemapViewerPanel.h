#pragma once
#include "Core/Layer/Layer.h"
#include "Core/Application.h"
#include <array>
#include <cstdint>
#include <string>

// Inspector for one .tcubemap asset: shows the six faces as a cross and lets
// them be reassigned and saved. It knows nothing about the skybox; whoever
// uses the cubemap picks up saves through AssetDatabase::ContentRevision.
class CubemapViewerPanel : public Engine::Layer
{
public:
	CubemapViewerPanel()
		: Layer("Cubemap")
	{
		Instance() = this;
	}

	~CubemapViewerPanel() override
	{
		if (Instance() == this)
			Instance() = nullptr;
	}

	static void Open(const std::string& path)
	{
		if (path.empty())
			return;
		if (!Instance())
			Engine::Application::Get().PushLayer(new CubemapViewerPanel());
		if (Instance())
			Instance()->Show(path);
	}

	void OnImGuiRender() override;

private:
	T_API void Show(const std::string& path);
	void Reload();
	void DrawFaces();
	void DrawFaceEditor();

	void DestroySelf()
	{
		Engine::Application::Get().PopLayer(this);
		delete this;
	}

	// Defined in the DLL. A header inline static would be a second copy in SandBox.
	T_API static CubemapViewerPanel*& Instance();

	std::string m_Path;
	bool m_Open = false;
	// Face paths in +X,-X,+Y,-Y,+Z,-Z order. Empty = UID did not resolve.
	std::array<std::string, 6> m_SavedFaces;
	std::array<std::string, 6> m_EditFaces;
	std::array<uint64_t, 6> m_SavedUids{};
	std::string m_LoadError;
	std::string m_SaveError;
};
