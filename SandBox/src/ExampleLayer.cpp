#include "ExampleLayer.h"
#include <Debug/Debug.h>
#include<Trans/SceneCamera.h>
#include<Core/Application.h>


entt::entity m_SelectedContext = null;
void ShowDockSpace()
{
	static bool opt_fullscreen = true;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;


	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	if (opt_fullscreen)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}
	else
	{
		dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
	}

	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", 0, window_flags);
	ImGui::PopStyleVar();
	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	// Submit the DockSpace
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}
	ImGui::End();

}
ExampleLayer::ExampleLayer(Ref<Scene>scene, std::string name) : BasePanel(name)
{
	this->m_Context = scene;
	m_SelectedContext = null;
	auto& app = Engine::Application::Get();
	m_WindowHandle = static_cast<GLFWwindow*>(app.GetWindow().GetWindow());

	if (!m_WindowHandle) {
		Error_Core(false, "ExampleLayer: 无法从 Application 获取到窗口句柄！");
	}


	shader = CreatePtr<Shader>("D:/Code/C++/Tsundere/res/shaders/Basic.shader");

	

	m_BaseFboSpec = { 1080, 960, 1 };
	m_MsaaFboSpec = { 1080, 960, 16 };
	framebuffer = CreateRef<FrameBuffer>(m_BaseFboSpec);



	geometrypass = CreateRef<GeometryPass>();
	geometrypass->Init(framebuffer);

	taaPass = CreateRef<TAAPass>();
	taaPass->Init(framebuffer);



	std::vector<std::string> texpaths{
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/right.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/left.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/top.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/bottom.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/front.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/back.jpg"
	};
	currentcamera->skybox = CreateRef<SkyBox>(texpaths);

	// Create default directional light
	auto lightEntity = scene->CreateEntity("Directional Light");
	lightEntity.AddComponent<Component::DirectionalLight>();

}



void ExampleLayer::OnUpdate()
{
	if (m_ViewPortSize.x <= 0.0f || m_ViewPortSize.y <= 0.0f)
		return;
	Renderer renderer;

	// 按需创建 / 释放 MSAA FBO
	if (open_Msaa && !Msaaframebuffer)
		Msaaframebuffer = CreatePtr<MsaaFrameBuffer>(m_MsaaFboSpec);
	else if (!open_Msaa && Msaaframebuffer)
		Msaaframebuffer.reset();

	if (open_Msaa)
		Msaaframebuffer->Bind();
	else
		framebuffer->Bind();

	renderer.Clear();
	{
		if (m_ViewportFocused)
			currentcamera->GLPrecessInput(m_WindowHandle, 0.1f);
		currentcamera->RenderSkyBox();

		geometrypass->Execute(m_Context, renderResources);
	}
	if (open_Msaa)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, Msaaframebuffer->GetFrameID());
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer->GetFrameID());
		glBlitFramebuffer(0, 0, m_ViewPortSize.x, m_ViewPortSize.y,
			0, 0, m_ViewPortSize.x, m_ViewPortSize.y,
			GL_COLOR_BUFFER_BIT, GL_NEAREST);
		Msaaframebuffer->UnBind();
	}
	else
		framebuffer->UnBind();

	renderResources.SourceFBO = framebuffer->GetFrameID();

	// TAA Pass
	if (taaPass)
		taaPass->Execute(m_Context, renderResources);
}

void ExampleLayer::OnImGuiRender()
{
	ShowDockSpace();

	ImGui::Begin("State");
	ImGui::Checkbox("OpenMsaa?", &open_Msaa);
	ImGui::Checkbox("TAA?", &taaPass->Enabled);
	ImGui::Checkbox("Jitter?", &geometrypass->EnableJitter);
	ImGui::Checkbox("rendew?", &rendertow);
	ImGui::End();

	ImGui::Begin(m_HeadTitle.c_str());
	
	if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
		m_SelectedContext = null;

	for (auto entityID : m_Context->m_Registry.view<Top>())
	{
		Entity entity = Entity{ m_Context.get(),entityID };

		DrawEntityNode(entity);
	}


	if (ImGui::BeginPopupContextWindow(0, 1))
	{
		if (ImGui::MenuItem("Create Empty Entity"))
		{
			//Entity entity = m_Context->CreateEntity("Empty Entity");
			//Entity childOne = m_Context->CreateEntity("ChildOne");

			//entity.addchildwithchangeparent(childOne);

			Entity entity = m_Context->CreateEntity("Empty Entity");
			if (m_SelectedContext != null)
			{
				Entity{ m_Context.get(),m_SelectedContext }.addchildwithchangeparent(entity);
			}

		}
		if (m_SelectedContext != null) {
			if (ImGui::MenuItem("Delete"))
			{
				for (auto entityID : m_Context->m_Registry.view<ID>())
				{
					Info_Core((int)entityID);
				}
				m_Context->DestoryEntity(Entity{ m_Context.get(),m_SelectedContext });
				m_SelectedContext = null;
			}
		}

		ImGui::EndPopup();
	}
	ImGui::End();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0,0 });

	ImGui::Begin("ViewPort");
	m_ViewportFocused = ImGui::IsWindowFocused();
	ImGuiIO& io = ImGui::GetIO();

#pragma region MouseInput
	if (m_ViewportFocused && ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		// io.MouseDelta 自动帮我们算好了这一帧和上一帧的差值，自带 firstMouse 效果！
		float xoffset = io.MouseDelta.x;
		float yoffset = -io.MouseDelta.y; // Y 轴需要翻转

		if (currentcamera)
			currentcamera->GLMouseInput(xoffset, yoffset, true);
	}

	// 2. 处理鼠标滚轮缩放
	if (m_ViewportFocused && io.MouseWheel != 0.0f)
	{
		if (currentcamera)
			currentcamera->GLScrollInput(0.0f, io.MouseWheel);
	}
#pragma endregion

	ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
	if (m_ViewPortSize != *((vec2*)&viewportPanelSize))
	{
		m_ViewPortSize = { viewportPanelSize.x,viewportPanelSize.y };
		glViewport(0, 0, m_ViewPortSize.x, m_ViewPortSize.y);
		m_BaseFboSpec.Width = m_ViewPortSize.x;
		m_BaseFboSpec.Height = m_ViewPortSize.y;
		m_MsaaFboSpec.Width = m_ViewPortSize.x;
		m_MsaaFboSpec.Height = m_ViewPortSize.y;
		framebuffer->Rsetsize(m_ViewPortSize);
		if (Msaaframebuffer) Msaaframebuffer->Rsetsize(m_ViewPortSize);
		if (taaPass) taaPass->OnResize(m_ViewPortSize.x, m_ViewPortSize.y);
		currentcamera->SetAspect(m_ViewPortSize.x, m_ViewPortSize.y);
	}


	ImGui::Image((ImTextureID)(uintptr_t)renderResources.SceneColorTexture, ImVec2(m_ViewPortSize.x, m_ViewPortSize.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	
	ImGui::End();
	ImGui::PopStyleVar();
}

void ExampleLayer::OnEvent(Eventing::Event<>& event)
{
	event.Invoke();
}
#define Drop
#ifdef Drop
void ExampleLayer::DrawEntityNode(Entity entity)
{
	if (m_SelectedContext != null)
	{
		bool a = Entity{ m_Context.get(),m_SelectedContext }.HasComponent <Tag>();
	}


	auto& tag = entity.GetComponent<Tag>();
	ImGuiTreeNodeFlags flags = (m_SelectedContext == entity ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
	bool opened = ImGui::TreeNodeEx((void*)(entt::entity)entity, flags, tag.tag.c_str());

	if (ImGui::IsItemClicked())
	{
		m_SelectedContext = entity;
		Info_Core("Switch Select" + Entity{ m_Context.get(),m_SelectedContext }.GetComponent<Tag>().tag);
	}
	bool Deleted = false;
	/*if (ImGui::BeginPopupContextWindow(0, 1))
	{

		if (ImGui::MenuItem("Delete Empty"))
			Deleted = true;
		ImGui::EndPopup();
	}*/
	if (opened)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

		for (auto childID : entity.GetComponent<Child>().children)
		{
			Entity child = Entity{ m_Context.get(),childID };
			DrawEntityNode(child);
		}


		ImGui::TreePop();
	}
	if (Deleted)
	{
		for (auto childID : entity.GetComponent<Child>().children)
		{
			Entity child = Entity{ m_Context.get(),childID };
			m_Context->DestoryEntity(child);
		}
		m_Context->DestoryEntity({ m_Context.get(),m_SelectedContext });
	}
}


#endif // Drop