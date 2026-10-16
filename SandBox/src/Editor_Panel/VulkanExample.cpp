#include"VulkanExample.h"
#include<Core/Application.h>
#include<Pipeline/Passes/GbufferPass.h>
#include<Pipeline/Passes/DeferredLight.h>
#include <Pipeline/RenderPassRegistry.h>
#include <Platform/RHI/RHIImGuiRenderer.h>
#include <Panels/EditorWindows.h>


VulkanExampleLayer::VulkanExampleLayer(Ref<Scene> scene, std::string name)
{
	this->m_Context = scene;
	m_SelectedContext = null;

	auto& app = Engine::Application::Get();
	m_WindowHandle = static_cast<GLFWwindow*>(app.GetWindow().GetWindow());

	if (!m_WindowHandle) {
		Error_Core("ExampleLayer: �޷��� Application ��ȡ�����ھ����");
	}

	// 初始化 Pass 管理器
	PassCreationContext ctx;
	ctx.scene = m_Context;
	ctx.frameData = nullptr;  // 延迟创建
	ctx.width = 1920;
	ctx.height = 1080;
	m_PassManager.RebuildPasses(ctx);
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
	ImGui::Begin("ViewPort");
	m_ViewportFocused = ImGui::IsWindowFocused();

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

	if (currentcamera && !currentcamera->skybox)
	{
		std::vector<std::string> texpaths{
			"F:/Tsundere/res/texture/CubeMap/Ori/right.jpg",
			"F:/Tsundere/res/texture/CubeMap/Ori/left.jpg",
			"F:/Tsundere/res/texture/CubeMap/Ori/top.jpg",
			"F:/Tsundere/res/texture/CubeMap/Ori/bottom.jpg",
			"F:/Tsundere/res/texture/CubeMap/Ori/front.jpg",
			"F:/Tsundere/res/texture/CubeMap/Ori/back.jpg"
		};
		currentcamera->skybox = CreateRef<SkyBox>(std::move(texpaths));
		m_GraphDirty = true;
	}

	if (m_GraphDirty
		|| width != m_GraphBuiltWidth
		|| height != m_GraphBuiltHeight)
	{
		BuildRenderGraph(width, height);
	}

	if (m_ViewportFocused)
		currentcamera->ProcessKeyboard(m_WindowHandle, 0.5f);

	m_RenderGraph.Execute(*context);

	RHITexture2D* finalColor = m_RenderGraph.GetExportedTexture(m_GraphFinalColor);
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

	// 从 Pass 管理器获取启用的 Pass
	auto passes = m_PassManager.GetEnabledPasses();
	for (auto& pass : passes)
	{
		m_RenderGraph.AddPass(pass);
	}

	m_RenderGraph.Compile();

	m_GraphFinalColor = m_RenderGraph.GetTextureByName("DeferredLight.Light");

	if (m_GraphFinalColor.id == UINT32_MAX)
	{
		m_GraphFinalColor = m_RenderGraph.GetTextureByName("Geometry.SceneColor");
	}
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
