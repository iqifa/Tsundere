#include"VulkanExample.h"
#include "SkyboxSettings.h"
#include<Core/Application.h>
#include<Pipeline/Passes/GbufferPass.h>
#include<Pipeline/Passes/DeferredLight.h>
#include <Pipeline/RenderPassRegistry.h>
#include <Platform/RHI/RHIImGuiRenderer.h>
#include <Panels/EditorWindows.h>
#include <Core/Assets/AssetDatabase.h>
#include <Platform/FileDialog.h>
#include <algorithm>
#include <filesystem>


VulkanExampleLayer::VulkanExampleLayer(Ref<Scene> scene, std::string name)
{
	this->m_Context = scene;
	m_SelectedContext = null;

	auto& app = Engine::Application::Get();
	m_WindowHandle = static_cast<GLFWwindow*>(app.GetWindow().GetWindow());

	if (!m_WindowHandle) {
		Error_Core("ExampleLayer: �޷��� Application ��ȡ�����ھ����");
	}

	m_GraphFrameData = CreateRef<RGFrameData>();

	// 初始化 Pass 管理器
	PassCreationContext ctx;
	ctx.scene = m_Context;
	ctx.frameData = m_GraphFrameData;
	ctx.width = 1920;
	ctx.height = 1080;
	m_PassManager.RebuildPasses(ctx);
	try
	{
		m_SkyboxConfigPath = SkyboxSettings::ConfigPath().string();
		if (currentcamera)
		{
			currentcamera->SkyboxAssetUid = SkyboxSettings::Load(m_SkyboxConfigPath);
			m_AttemptedSkyboxAsset = currentcamera->SkyboxAssetUid;
		}
	}
	catch (const std::exception& error)
	{
		m_SkyboxSaveStatus = error.what();
		Warn_Core("[SkyBox settings]: {}", error.what());
	}
}

void VulkanExampleLayer::OnUpdate()
{
	if (m_ViewPortSize.x <= 0.0f || m_ViewPortSize.y <= 0.0f)
		return;

	ExecuteRenderGraph();
}

void VulkanExampleLayer::OnImGuiRender()
{
	// The window stays empty until the graph is built, so ImGui would auto-fit it to 0 height
	// (and imgui.ini would persist that), leaving OnUpdate to bail out forever.
	ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 180.0f), ImVec2(FLT_MAX, FLT_MAX));
	ImGui::Begin("ViewPort", nullptr, ImGuiWindowFlags_MenuBar);
	m_ViewportFocused = ImGui::IsWindowFocused();

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Post Processing"))
		{
			const auto toggle = [this](const char* label, bool& enabled)
			{
				if (ImGui::MenuItem(label, nullptr, &enabled))
					m_GraphDirty = true;
			};
			toggle("Depth of Field", m_PostProcess.depthOfField);
			toggle("Motion Blur", m_PostProcess.motionBlur);
			toggle("Bloom", m_PostProcess.bloom);
			toggle("Blur", m_PostProcess.blur);
			ImGui::Separator();
			toggle("Tone Mapping", m_PostProcess.toneMapping);
			toggle("Color Grading", m_PostProcess.colorGrading);
			toggle("Sharpen", m_PostProcess.sharpen);
			toggle("Chromatic Aberration", m_PostProcess.chromaticAberration);
			toggle("Vignette", m_PostProcess.vignette);
			toggle("FXAA", m_PostProcess.fxaa);
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	if (currentcamera && m_ViewportFocused)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
		{
			const float xoffset = io.MouseDelta.x;
			const float yoffset = -io.MouseDelta.y;
			currentcamera->ProcessMouseLook(xoffset, yoffset);
		}

		if (io.MouseWheel != 0.0f)
			currentcamera->ProcessScroll(io.MouseWheel);
	}

	const ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
	const glm::vec2 newViewportSize = {
		viewportPanelSize.x,
		viewportPanelSize.y
	};
	if (m_ViewPortSize != newViewportSize)
	{
		m_ViewPortSize = newViewportSize;
		m_GraphDirty = true;

		if (currentcamera && m_ViewPortSize.x > 0.0f && m_ViewPortSize.y > 0.0f)
			currentcamera->SetAspect(m_ViewPortSize.x, m_ViewPortSize.y);
	}

	if (m_FinalColorImGuiID != ImTextureID_Invalid &&
		m_GraphBuiltWidth > 0 && m_GraphBuiltHeight > 0)
	{
		ImGui::Image(
			m_FinalColorImGuiID,
			ImVec2(m_ViewPortSize.x, m_ViewPortSize.y),
			ImVec2(0.0f, 1.0f),
			ImVec2(1.0f, 0.0f));
	}

	ImGui::End();

	DrawSkyboxSettings();

	if (EditorWindows::ShadowMap())
	{
		ImGui::SetNextWindowSize(ImVec2(512.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Shadow Map", &EditorWindows::ShadowMap()))
		{
			if (m_ShadowMapImGuiID != ImTextureID_Invalid)
			{
				const ImVec2 avail = ImGui::GetContentRegionAvail();
				const float side = avail.x > 1.0f ? avail.x : 256.0f;
				ImGui::Image(
					m_ShadowMapImGuiID,
					ImVec2(side, side),
					ImVec2(0.0f, 1.0f),
					ImVec2(1.0f, 0.0f));

				if (m_ShadowMapTexture)
				{
					ImGui::Text("%u x %u  ShadowMap.ShadowVis",
						m_ShadowMapTexture->GetWidth(),
						m_ShadowMapTexture->GetHeight());
				}
			}
			else
			{
				ImGui::TextUnformatted("Shadow map is not ready.");
			}
		}
		ImGui::End();
	}

	// 渲染 Pass 管理面板
	if (m_ShowPassManager)
	{
		m_PassManager.OnImGuiRender();

		// 如果 Pass 管理器状态改变，标记需要重建
		if (m_PassManager.NeedsRebuild())
		{
			m_GraphDirty = true;
			m_PassManager.ClearRebuildFlag();
		}
	}
}
void VulkanExampleLayer::OnEvent(Eventing::Event<>& event)
{
	event.Invoke();
}

void VulkanExampleLayer::ExecuteRenderGraph()
{
	const uint32_t width = static_cast<uint32_t>(m_ViewPortSize.x);
	const uint32_t height = static_cast<uint32_t>(m_ViewPortSize.y);

	if (width == 0 || height == 0)
		return;

	Ref<RHIContext> context = RHIRenderer::GetContext();
	if (!context)
	{
		Error_Core("[Vulkan Example RenderGraph]: RHI context is null");
		return;
	}

	if (currentcamera && (!m_SkyboxChecked || currentcamera->SkyboxAssetUid != m_AttemptedSkyboxAsset ||
		currentcamera->SkyboxAssetRevision != m_SkyboxAssetRevision))
	{
		const bool selectionChanged = currentcamera->SkyboxAssetUid != m_AttemptedSkyboxAsset;
		m_SkyboxChecked = true;
		m_AttemptedSkyboxAsset = currentcamera->SkyboxAssetUid;
		m_SkyboxAssetRevision = currentcamera->SkyboxAssetRevision;
		if (selectionChanged)
			SaveSkyboxSettings();
		m_SkyboxStatus.clear();
		try
		{
			Ref<SkyBox> replacement;
			std::error_code fileError;
			const auto resolved = Engine::AssetDatabase::Resolve(m_AttemptedSkyboxAsset, Engine::AssetKind::Cubemap);
			if (m_AttemptedSkyboxAsset == 0)
			{
				m_SkyboxStatus = "No skybox selected.";
				Warn_Core("[SkyBox]: no cubemap selected");
			}
			else if (!std::filesystem::is_regular_file(resolved, fileError))
			{
				m_SkyboxStatus = "Cubemap UUID not found or inaccessible: " + std::to_string(m_AttemptedSkyboxAsset);
				Warn_Core("[SkyBox]: {} ({})", m_SkyboxStatus,
					fileError ? fileError.message() : "file not found");
			}
			else
				replacement = CreateRef<SkyBox>(resolved);
			if (replacement || m_AttemptedSkyboxAsset == 0)
			{
				currentcamera->skybox = std::move(replacement);
				m_LoadedSkyboxAsset = resolved;
				m_GraphDirty = true;
			}
		}
		catch (const std::exception& error)
		{
			m_SkyboxStatus = error.what();
			if (dynamic_cast<const std::filesystem::filesystem_error*>(&error))
			{
				Warn_Core("[SkyBox]: {}", error.what());
			}
			else
			{
				Error_Core("[SkyBox]: failed to load {}: {}", m_AttemptedSkyboxAsset, error.what());
			}
		}
	}

	if (m_GraphDirty
		|| width != m_GraphBuiltWidth
		|| height != m_GraphBuiltHeight)
	{
		BuildRenderGraph(width, height);
	}

	if (m_ViewportFocused)
		currentcamera->ProcessKeyboard(m_WindowHandle, 0.5f);

	m_GraphFrameData->CameraJitterEnabled = m_PassManager.IsPassEnabled("TAA");
	m_GraphFrameData->BeginCameraFrame(currentcamera->GetViewFront(),
		currentcamera->GetProj(), width, height, currentcamera);
	m_RenderGraph.Execute(*context);
	m_GraphFrameData->EndCameraFrame();

	RHITexture2D* finalColor = m_GraphFinalColor.id != InvalidResourceId
		? m_RenderGraph.GetExportedTexture(m_GraphFinalColor)
		: nullptr;
	if (finalColor != m_FinalColorTexture)
	{
		if (m_FinalColorTexture)
			RHIImGUIRenderer::ReleaseTexture(m_FinalColorTexture);

		m_FinalColorTexture = finalColor;
		m_FinalColorImGuiID = finalColor
			? RHIImGUIRenderer::GetTextureID(finalColor)
			: ImTextureID_Invalid;
	}

	if (finalColor && m_FinalColorImGuiID == ImTextureID_Invalid)
		m_FinalColorImGuiID = RHIImGUIRenderer::GetTextureID(finalColor);

	RHITexture2D* shadowMap = nullptr;
	if (m_ShadowMap.id != InvalidResourceId)
		shadowMap = m_RenderGraph.GetExportedTexture(m_ShadowMap);

	if (shadowMap != m_ShadowMapTexture)
	{
		if (m_ShadowMapTexture)
			RHIImGUIRenderer::ReleaseTexture(m_ShadowMapTexture);

		m_ShadowMapTexture = shadowMap;
		m_ShadowMapImGuiID = shadowMap
			? RHIImGUIRenderer::GetTextureID(shadowMap)
			: ImTextureID_Invalid;
	}

	if (shadowMap && m_ShadowMapImGuiID == ImTextureID_Invalid)
		m_ShadowMapImGuiID = RHIImGUIRenderer::GetTextureID(shadowMap);
}

void VulkanExampleLayer::SaveSkyboxSettings()
{
	try
	{
		if (!currentcamera || m_SkyboxConfigPath.empty())
			throw std::runtime_error("Skybox settings are unavailable");
		SkyboxSettings::Save(m_SkyboxConfigPath, currentcamera->SkyboxAssetUid);
		m_SkyboxSaveStatus = "Settings saved.";
	}
	catch (const std::exception& error)
	{
		m_SkyboxSaveStatus = error.what();
		Error_Core("[SkyBox settings]: {}", error.what());
	}
}

void VulkanExampleLayer::DrawSkyboxSettings()
{
	if (!EditorWindows::Skybox())
		return;
	ImGui::SetNextWindowSize(ImVec2(440, 240), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Skybox", &EditorWindows::Skybox()))
	{
		if (currentcamera)
		{
			const auto select = [](uint64_t uid)
			{
				currentcamera->SkyboxAssetUid = uid;
				++currentcamera->SkyboxAssetRevision;
			};
			const auto selected = currentcamera->SkyboxAssetUid;
		const auto selectedPath = Engine::AssetDatabase::Resolve(selected, Engine::AssetKind::Cubemap);
			if (ImGui::BeginCombo("Cubemap", selected == 0 ? "None" : std::filesystem::path(selectedPath).filename().string().c_str()))
			{
				if (ImGui::Selectable("None", selected == 0))
					select(0);
				for (const auto& asset : Engine::AssetDatabase::GetByKind(Engine::AssetKind::Cubemap))
				{
					ImGui::PushID(asset.path.c_str());
					if (ImGui::Selectable(std::filesystem::path(asset.path).filename().string().c_str(), selected == asset.uid))
						select(asset.uid);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset.path.c_str());
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			if (ImGui::Button("Reload"))
				++currentcamera->SkyboxAssetRevision;
			ImGui::SameLine();
			if (ImGui::Button("Clear"))
				select(0);
			if (ImGui::Button("Save Settings"))
				SaveSkyboxSettings();
			if (!m_SkyboxSaveStatus.empty())
				ImGui::TextWrapped("%s", m_SkyboxSaveStatus.c_str());
			ImGui::Separator();
			ImGui::TextWrapped("Active: %s", m_LoadedSkyboxAsset.empty() ? "None" : std::filesystem::path(m_LoadedSkyboxAsset).filename().string().c_str());
			if (!m_SkyboxStatus.empty())
				ImGui::TextWrapped("%s", m_SkyboxStatus.c_str());
			ImGui::TextWrapped("Create reference cubemaps using Assets > Import Cubemap.");
		}
		else
			ImGui::TextUnformatted("No active camera.");
	}
	ImGui::End();
}

void VulkanExampleLayer::BuildRenderGraph(uint32_t width, uint32_t height)
{
	if (m_FinalColorTexture)
	{
		RHIImGUIRenderer::ReleaseTexture(m_FinalColorTexture);
		m_FinalColorTexture = nullptr;
		m_FinalColorImGuiID = ImTextureID_Invalid;
	}

	if (m_ShadowMapTexture)
	{
		RHIImGUIRenderer::ReleaseTexture(m_ShadowMapTexture);
		m_ShadowMapTexture = nullptr;
		m_ShadowMapImGuiID = ImTextureID_Invalid;
	}

	m_RenderGraph.Reset();

	if (!m_GraphFrameData)
		m_GraphFrameData = CreateRef<RGFrameData>();

	// 更新 Pass 管理器的上下文
	PassCreationContext ctx;
	ctx.scene = m_Context;
	ctx.frameData = m_GraphFrameData;
	ctx.width = width;
	ctx.height = height;
	m_PassManager.UpdateContext(ctx);

	// 从 Pass 管理器获取启用的 Pass
	auto passes = m_PassManager.GetEnabledPasses();
	for (auto& pass : passes)
	{
		m_RenderGraph.AddPass(pass);
	}

	if (!m_PostProcessShader)
		m_PostProcessShader = RHIShader::Create("res/shaders/PostProcess.shader");
	std::string postProcessColor;
	if (m_PostProcessShader && !m_PostProcessShader->GetReflection().PushConstants.empty())
		postProcessColor = PostProcess::AddPasses(m_RenderGraph, m_PostProcessShader,
			width, height, m_PostProcess, m_PassManager.IsPassEnabled("DeferredLight"),
			currentcamera->GetProj());

	m_RenderGraph.Compile();

	m_GraphFinalColor = postProcessColor.empty()
		? RGTextureHandle{} : m_RenderGraph.GetTextureByName(postProcessColor);
	if (m_GraphFinalColor.id == InvalidResourceId)
		m_GraphFinalColor = m_RenderGraph.GetTextureByName(PostProcess::SceneColor);
	if (m_GraphFinalColor.id == InvalidResourceId)
		m_GraphFinalColor = m_RenderGraph.GetTextureByName(PostProcess::FallbackSceneColor);
	m_GraphVelocity = m_RenderGraph.GetTextureByName("Geometry.Velocity");
	m_ShadowMap = m_RenderGraph.GetTextureByName("ShadowMap.ShadowVis");

	m_GraphBuiltWidth = width;
	m_GraphBuiltHeight = height;
	m_GraphDirty = false;

	// 打印已注册和启用的 Pass 信息
	auto registeredPasses = RenderPassRegistry::Get().GetRegisteredPassNames();
	Info_Core("[Vulkan Example RenderGraph]: Built graph with {} passes (Total registered: {})",
		passes.size(), registeredPasses.size());
}
