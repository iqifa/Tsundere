#include"VulkanExample.h"
#include<Core/Application.h>
#include <Pipeline/Passes/GeometryPassV2.h>
#include <Platform/RHI/RHIImGuiRenderer.h>



VulkanExampleLayer::VulkanExampleLayer(Ref<Scene> scene, std::string name)
{
	this->m_Context = scene;
	m_SelectedContext = null;

	auto& app = Engine::Application::Get();
	m_WindowHandle = static_cast<GLFWwindow*>(app.GetWindow().GetWindow());

	if (!m_WindowHandle) {
		Error_Core("ExampleLayer: �޷��� Application ��ȡ�����ھ����");
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
	ImGui::Begin("ViewPort");
	m_ViewportFocused = ImGui::IsWindowFocused();

	const ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
	const vec2 newViewportSize = {
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
			"D:/Code/C++/Tsundere/res/texture/CubeMap/Ori/right.jpg",
			"D:/Code/C++/Tsundere/res/texture/CubeMap/Ori/left.jpg",
			"D:/Code/C++/Tsundere/res/texture/CubeMap/Ori/top.jpg",
			"D:/Code/C++/Tsundere/res/texture/CubeMap/Ori/bottom.jpg",
			"D:/Code/C++/Tsundere/res/texture/CubeMap/Ori/front.jpg",
			"D:/Code/C++/Tsundere/res/texture/CubeMap/Ori/back.jpg"
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
		currentcamera->GLPrecessInput(m_WindowHandle, 0.5f);

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

	renderResources.SceneColorTexture = finalColor
		? static_cast<unsigned int>(finalColor->GetNativeID())
		: 0;

	RHITexture2D* velocity = m_RenderGraph.GetExportedTexture(m_GraphVelocity);
	renderResources.VelocityTexture = velocity
		? static_cast<unsigned int>(velocity->GetNativeID())
		: 0;
}

void VulkanExampleLayer::BuildRenderGraph(uint32_t width, uint32_t height)
{
	if (m_FinalColorTexture)
	{
		RHIImGUIRenderer::ReleaseTexture(m_FinalColorTexture);
		m_FinalColorTexture = nullptr;
		m_FinalColorImGuiID = ImTextureID_Invalid;
	}

	m_RenderGraph.Reset();

	if (!m_GraphFrameData)
		m_GraphFrameData = CreateRef<RGFrameData>();
	auto geometryPassV2 = CreateRef<GeometryPassV2>(m_Context, m_GraphFrameData);
	geometryPassV2->SetViewportSize(width, height);
	m_RenderGraph.AddPass(geometryPassV2);

	m_RenderGraph.Compile();


	m_GraphFinalColor = m_RenderGraph.GetTextureByName("Geometry.SceneColor");
	m_GraphVelocity = m_RenderGraph.GetTextureByName("Geometry.Velocity");

	m_GraphBuiltWidth = width;
	m_GraphBuiltHeight = height;
	m_GraphDirty = false;


	Info_Core("[Vulkan Example RenderGraph]: Built graph with {} passes", 1);
}
