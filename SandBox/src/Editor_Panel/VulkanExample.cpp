#include"VulkanExample.h"
#include "SkyboxSettings.h"
#include<Core/Application.h>
#include<Pipeline/Passes/GbufferPass.h>
#include<Pipeline/Passes/DeferredLight.h>
#include <string_view>
#include <Pipeline/Passes/TAAPass.h>
#include <Pipeline/RenderPassRegistry.h>
#include <Platform/RHI/RHIImGuiRenderer.h>
#include <Panels/EditorWindows.h>
#include <Core/Assets/AssetDatabase.h>
#include <Platform/FileDialog.h>
#include <algorithm>
#include <filesystem>


namespace
{
    constexpr const char* PreviewLabels[] = {"Final color", "Scene color (before post)", "World normal + model",
        "Base color + AO", "Metallic / roughness", "Velocity", "Emissive", "Depth"};
    constexpr const char* PreviewFormats[] = {"RGBA8 UNORM", "R8 UNORM", "R16F", "RG16F", "RGBA16F", "RGBA32F"};
    constexpr Format PreviewFormatValues[] = {Format::RGBA8_UNORM, Format::R8_UNORM, Format::R16F,
        Format::RG16F, Format::RGBA16F, Format::RGBA32F};
    const char* PreviewFormatName(Format f)
    {
        for (int i = 0; i < IM_ARRAYSIZE(PreviewFormatValues); ++i)
            if (f == PreviewFormatValues[i]) return PreviewFormats[i];
        switch (f) {
        case Format::D24_UNORM_S8_UINT: return "D24S8";
        case Format::D32_SFLOAT: return "D32F";
        case Format::D32_SFLOAT_S8_UINT: return "D32FS8";
        case Format::RGBA8_SRGB: return "RGBA8 sRGB";
        default: return "Unavailable";
        }
    }
}

VulkanExampleLayer::VulkanExampleLayer(Ref<Scene> scene, std::string name)
{
	this->m_Context = scene;
	m_SelectedContext = null;

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

void VulkanExampleLayer::DrawPostProcessMenu()
{
	ImGui::MenuItem("Post Processing", nullptr, &m_ShowPostProcessPanel);
}

void VulkanExampleLayer::DrawPostProcessPanel()
{
    if (!m_ShowPostProcessPanel) return;
    ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Post Processing", &m_ShowPostProcessPanel))
    {
        const char* effects[] = {"Bloom", "Blur", "Depth of Field", "Motion Blur", "Tone Mapping",
            "Color Grading", "Sharpen", "Chromatic Aberration", "Vignette", "FXAA"};
        auto& s = m_PostProcess;
        bool* enabled[] = {&s.bloom, &s.blur, &s.depthOfField, &s.motionBlur, &s.toneMapping,
            &s.colorGrading, &s.sharpen, &s.chromaticAberration, &s.vignette, &s.fxaa};
        bool changed = false;
        if (ImGui::BeginTable("EffectsAndParameters", 2,
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Effects", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            for (int effect = 0; effect < IM_ARRAYSIZE(effects); ++effect)
            {
                ImGui::PushID(effect);
                changed |= ImGui::Checkbox("##Enabled", enabled[effect]);
                ImGui::SetItemTooltip("Toggle %s", effects[effect]);
                ImGui::SameLine();
                if (ImGui::Selectable(effects[effect], m_PostProcessSelection == effect))
                    m_PostProcessSelection = effect;
                ImGui::PopID();
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(effects[m_PostProcessSelection]);
            ImGui::Separator();
            ImGui::PushID(m_PostProcessSelection);
            const auto slider = [&](const char* label, float& value, float lo, float hi)
            {
                ImGui::TextUnformatted(label);
                ImGui::PushID(label);
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::SliderFloat("##Value", &value, lo, hi);
                ImGui::PopID();
            };
            switch (m_PostProcessSelection)
            {
            case 0:
                slider("Threshold", s.bloomThreshold, 0, 10); slider("Soft knee", s.bloomKnee, 0, 2);
                slider("Intensity", s.bloomStrength, 0, 3); slider("Radius (half-res pixels)", s.bloomRadius, 0, 8); break;
            case 1: slider("Radius (pixels)", s.blurRadius, 0, 8); break;
            case 2:
                slider("Focus distance", s.focusDistance, 0.01f, 100); slider("Focus range", s.focusRange, 0.01f, 2);
                slider("Max radius (pixels)", s.dofRadius, 0, 32); break;
            case 3:
                slider("Shutter strength", s.motionStrength, 0, 2); slider("Max trail (pixels)", s.motionMaxPixels, 0, 64); break;
            case 4: slider("Exposure (EV)", s.exposure, -8, 8); slider("Display gamma", s.gamma, 0.1f, 4); break;
            case 5:
                slider("Saturation", s.saturation, 0, 2);
                ImGui::TextUnformatted("Tint");
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::ColorEdit3("##Tint", &s.colorTint.x, ImGuiColorEditFlags_Float); break;
            case 6: slider("Strength", s.sharpenStrength, 0, 2); break;
            case 7: slider("Offset (pixels)", s.aberrationPixels, 0, 10); break;
            case 8:
                slider("Strength", s.vignetteStrength, 0, 1); slider("Inner radius", s.vignetteInner, 0, 1.4f);
                slider("Outer radius", s.vignetteOuter, 0.01f, 2);
                s.vignetteOuter = std::max(s.vignetteOuter, s.vignetteInner + 0.001f); break;
            case 9:
                slider("Relative threshold", s.fxaaThreshold, 0, 0.5f);
                slider("Minimum threshold", s.fxaaMinThreshold, 0, 0.25f); slider("Max span (pixels)", s.fxaaSpan, 1, 16); break;
            }
            ImGui::PopID();
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (ImGui::Button("Reset all parameters")) { s = PostProcess::Settings{}; changed = true; }
        if (changed) m_GraphDirty = true;
    }
    ImGui::End();
}

void VulkanExampleLayer::DrawViewportPreviewControls()
{
	const auto previewLabel = std::string("Preview: ") + PreviewLabels[m_ViewportOutput] + "###PreviewSettings";
	if (ImGui::Button(previewLabel.c_str()))
		ImGui::OpenPopup("Viewport Preview");

	const bool fallback = m_ViewportConvertPass && m_ViewportOutput != 0 &&
		m_ViewportConvertPass->SourceName() != m_ViewportRequestedSource;
	if (fallback)
		ImGui::SetItemTooltip("Selected output unavailable. Showing final color.");

	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 27.0f, 0.0f), ImGuiCond_Always);
	if (!ImGui::BeginPopup("Viewport Preview"))
		return;

	bool changed = false;
	if (ImGui::BeginTable("PreviewFields", 2, ImGuiTableFlags_SizingFixedFit))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		const auto field = [](const char* label)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
		};

		field("Output");
		changed |= ImGui::Combo("##Output", &m_ViewportOutput, PreviewLabels, IM_ARRAYSIZE(PreviewLabels));
		field("Format");
		changed |= ImGui::Combo("##Format", &m_ViewportFormat, PreviewFormats, IM_ARRAYSIZE(PreviewFormats));
		if (m_ViewportFormat <= 1)
			ImGui::SetItemTooltip("UNORM clamps to [0, 1] and stores 8 bits per channel.");
		field("Channel");
		changed |= ImGui::Combo("##Channel", &m_ViewportChannel, "RGB\0RGBA\0R\0G\0B\0A\0");
		if (m_ViewportFormat == 1 || m_ViewportFormat == 2)
			ImGui::SetItemTooltip("This format stores R only. Select R for grayscale.");
		field("Display range");
		changed |= ImGui::DragFloat2("##DisplayRange", &m_ViewportRange.x, 0.01f);
		ImGui::SetItemTooltip("Maps these values to black and white. Use -1 to 1 for signed data.");
		ImGui::EndTable();
	}

	if (m_ViewportRange.y <= m_ViewportRange.x)
		m_ViewportRange.y = m_ViewportRange.x + 0.001f;
	if (changed)
		m_GraphDirty = true;

	if (m_ViewportConvertPass)
	{
		ImGui::Separator();
		ImGui::TextDisabled("Source");
		ImGui::TextWrapped("%s (%s) -> %s", m_ViewportConvertPass->SourceName().c_str(),
			PreviewFormatName(m_ViewportConvertPass->SourceFormat()), PreviewFormats[m_ViewportFormat]);
		if (fallback)
			ImGui::TextWrapped("Selected output unavailable. Showing final color.");
	}
	ImGui::EndPopup();
}

void VulkanExampleLayer::DrawSceneViewport()
{
	// The window stays empty until the graph is built, so ImGui would auto-fit it to 0 height
	// (and imgui.ini would persist that), leaving OnUpdate to bail out forever.
	ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 180.0f), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::Begin("ViewPort"))
	{
		m_ViewportCameraControl = false;
		m_TransformDrag.entity = entt::null;
		ImGui::End();
		return;
	}

	const auto toolButton = [this](const char* label, ViewportTool tool)
	{
		if (ImGui::Selectable(label, m_ViewportTool == tool, 0, ImGui::CalcTextSize(label)))
			m_ViewportTool = tool;
	};
	toolButton("Select (Q)", ViewportTool::Select);
	ImGui::SameLine();
	toolButton("Move (W)", ViewportTool::Move);
	ImGui::SameLine();
	toolButton("Rotate (E)", ViewportTool::Rotate);
	ImGui::SameLine();
	toolButton("Scale (R)", ViewportTool::Scale);

	DrawViewportPreviewControls();

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
		m_ViewPortSize.x > 0.0f && m_ViewPortSize.y > 0.0f)
	{
		ImGui::InvisibleButton("##SceneImage", viewportPanelSize);
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddImage(m_FinalColorImGuiID, min, max,
			ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

		const bool hovered = ImGui::IsItemHovered();
		const bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			ImGui::SetWindowFocus();
		m_ViewportCameraControl = currentcamera && ImGui::IsWindowFocused() && rightDown &&
			(m_ViewportCameraControl || hovered) &&
			m_TransformDrag.entity == entt::null;
		if (currentcamera)
		{
			DrawViewportTools(min, max, hovered);
			const ImGuiIO& io = ImGui::GetIO();
			if (m_ViewportCameraControl)
			{
				currentcamera->ProcessMouseLook(io.MouseDelta.x, -io.MouseDelta.y);
				auto* window = static_cast<GLFWwindow*>(ImGui::GetWindowViewport()->PlatformHandle);
				if (window)
					currentcamera->ProcessKeyboard(window, 0.5f);
			}
			if (hovered && m_TransformDrag.entity == entt::null && io.MouseWheel != 0.0f)
				currentcamera->ProcessScroll(io.MouseWheel);
		}
	}
	else
	{
		m_ViewportCameraControl = false;
		m_TransformDrag.entity = entt::null;
	}

	ImGui::End();
}

void VulkanExampleLayer::OnImGuiRender()
{
	DrawSceneViewport();
	DrawPostProcessPanel();

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
		if (ImGui::Begin("Render Pass Manager"))
		{
			m_PassManager.DrawControls();
			ImGui::Separator();
			ImGui::BeginDisabled(!m_PassManager.IsPassEnabled("Geometry"));
			if (ImGui::Checkbox("Geometry MSAA (4x)", &m_GeometryMSAAEnabled))
				m_GraphDirty = true;
			ImGui::EndDisabled();
		}
		ImGui::End();

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
	if (m_SceneRevision != m_Context->Revision)
	{
		m_SceneRevision = m_Context->Revision;
		m_GraphDirty = true;
	}
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

	m_GraphFrameData->CameraJitterEnabled = m_TAAOutputAvailable;
	m_GraphFrameData->BeginCameraFrame(currentcamera->GetViewFront(),
		currentcamera->GetProj(), width, height, currentcamera);
	m_GraphFrameData->BeginModelFrame(*m_Context);
	m_RenderGraph.Execute(*context);
	m_GraphFrameData->EndModelFrame();
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
	const bool deferred = m_PassManager.IsPassEnabled("Gbuffer") &&
		m_PassManager.IsPassEnabled("DeferredLight");
	for (auto& pass : passes)
	{
		const std::string_view name = pass->GetName();
		if ((deferred && name == "Geometry") ||
			(!deferred && (name == "Gbuffer" || name == "DeferredLight")))
			continue;
		if (auto geometry = std::dynamic_pointer_cast<GeometryPass>(pass))
			geometry->SetSampleCount(m_GeometryMSAAEnabled ? 4 : 1);
		m_RenderGraph.AddPass(pass);
		if (name == "DeferredLight")
		{
			auto forward = CreateRef<ForwardOpaquePass>(m_Context, m_GraphFrameData);
			forward->SetViewportSize(width, height);
			m_RenderGraph.AddPass(forward);
		}
	}

	if (!m_PostProcessShader)
		m_PostProcessShader = RHIShader::Create("res/shaders/PostProcess.shader");
	const char* postSource = "Geometry.SceneColor";
	if (deferred)
		postSource = "ForwardOpaque.SceneColor";
	if (m_PassManager.IsPassEnabled("Transparent"))
		postSource = "Transparent.SceneColor";
	if (m_PassManager.IsPassEnabled("TAA"))
		postSource = "TAA.SceneColor";
	std::string postProcessColor;
	if (m_PostProcessShader && !m_PostProcessShader->GetReflection().PushConstants.empty())
		postProcessColor = PostProcess::AddPasses(m_RenderGraph, m_PostProcessShader,
			width, height, m_PostProcess, deferred,
			currentcamera->GetProj(), postSource);

    if (!m_ViewportPreviewShader) m_ViewportPreviewShader = RHIShader::Create("res/shaders/ViewportPreview.shader");
    const char* previewSources[] = {"", deferred ? "ForwardOpaque.SceneColor" : "Geometry.SceneColor",
        "Gbuffer.Normal", "Gbuffer.SceneColor", "Gbuffer.MetallicRoughness",
        deferred ? "ForwardOpaque.Velocity" : "Geometry.Velocity", "Gbuffer.Emissive",
        deferred ? "ForwardOpaque.Depth" : "Geometry.Depth"};
    m_ViewportRequestedSource = previewSources[m_ViewportOutput];
    m_ViewportConvertPass = CreateRef<ViewportPreviewPass>("ViewportStorage", m_ViewportPreviewShader,
        std::vector<std::string>{m_ViewportRequestedSource, postProcessColor, "TAA.SceneColor",
            "Transparent.SceneColor", "ForwardOpaque.SceneColor", "Geometry.SceneColor"},
        PreviewFormatValues[m_ViewportFormat], glm::vec4(-1, 0, 1, 0), width, height);
    m_RenderGraph.AddPass(m_ViewportConvertPass);
    m_RenderGraph.AddPass(CreateRef<ViewportPreviewPass>("ViewportDisplay", m_ViewportPreviewShader,
        std::vector<std::string>{"ViewportStorage.Color"}, Format::RGBA16F,
        glm::vec4(static_cast<float>(m_ViewportChannel), m_ViewportRange.x, m_ViewportRange.y, 0), width, height));

	m_RenderGraph.Compile();

	const auto taaColor = m_RenderGraph.GetTextureByName("TAA.SceneColor");
	m_TAAOutputAvailable = taaColor.id != InvalidResourceId;
	m_GraphFinalColor = postProcessColor.empty()
		? RGTextureHandle{} : m_RenderGraph.GetTextureByName(postProcessColor);
	if (m_GraphFinalColor.id == InvalidResourceId)
		m_GraphFinalColor = taaColor;
	if (m_GraphFinalColor.id == InvalidResourceId)
		m_GraphFinalColor = m_RenderGraph.GetTextureByName("Transparent.SceneColor");
	if (m_GraphFinalColor.id == InvalidResourceId)
		m_GraphFinalColor = m_RenderGraph.GetTextureByName("ForwardOpaque.SceneColor");
	if (m_GraphFinalColor.id == InvalidResourceId)
		m_GraphFinalColor = m_RenderGraph.GetTextureByName("Geometry.SceneColor");
    const auto previewColor = m_RenderGraph.GetTextureByName("ViewportDisplay.Color");
    if (previewColor.id != InvalidResourceId) m_GraphFinalColor = previewColor;
	m_GraphVelocity = m_RenderGraph.GetTextureByName(deferred ? "ForwardOpaque.Velocity" : "Geometry.Velocity");
	m_ShadowMap = m_RenderGraph.GetTextureByName("ShadowMap.ShadowVis");

	m_GraphBuiltWidth = width;
	m_GraphBuiltHeight = height;
	m_GraphDirty = false;

	// 打印已注册和启用的 Pass 信息
	auto registeredPasses = RenderPassRegistry::Get().GetRegisteredPassNames();
	Info_Core("[Vulkan Example RenderGraph]: Built graph with {} passes (Total registered: {})",
		passes.size(), registeredPasses.size());
}
