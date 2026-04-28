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

	
	

	proj = ortho(0.0f, 1080.0f, 0.0f, 960.0f, -1.0f, 1.0f);
	view = translate(mat4(1.0f), vec3(-100, 0, 0));

	m_BaseFboSpec = { 1080, 960, 1 };
	m_MsaaFboSpec = { 1080, 960, 16 };
	framebuffer = CreateRef<FrameBuffer>(m_BaseFboSpec);



	geometrypass = CreateRef<GeometryPass>();
	geometrypass->Init(framebuffer);



	// --- 初始化全屏四边形用于后处理 ---
	float quadVertices[] = {
		// pos         // tex
		-1.0f,  1.0f,  0.0f, 1.0f,
		-1.0f, -1.0f,  0.0f, 0.0f,
		 1.0f, -1.0f,  1.0f, 0.0f,
		 1.0f,  1.0f,  1.0f, 1.0f
	};
	unsigned int quadIndices[] = { 0, 1, 2, 2, 3, 0 };
	m_QuadVA = CreatePtr<VertexArray>(4);
	m_QuadVB = CreatePtr<VertexBuffer>(quadVertices, sizeof(quadVertices));
	m_QuadIB = CreatePtr<IndexBuffer>(quadIndices, 6);
	VertexBufferLayout quadLayout;
	quadLayout.Push<float>(2); // pos
	quadLayout.Push<float>(2); // tex
	m_QuadVA->AddBuffer(*m_QuadVB, quadLayout);
	m_TaaShader = CreatePtr<Shader>("D:\\Code\\C++\\Tsundere\\res\\shaders\\TAA.shader");


	std::vector<std::string> texpaths{
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/right.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/left.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/top.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/bottom.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/front.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/back.jpg"
	};
	currentcamera->skybox = CreateRef<SkyBox>(texpaths);

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

	view = currentcamera->GetViewFront();

	proj = currentcamera->GetProj();
	mat4 currentViewProj = proj * view;

	if (open_TAA)
		proj = Jittering(proj, m_ViewPortSize.x, m_ViewPortSize.y);

	model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
	renderer.Clear();
	{
		if (m_ViewportFocused)
			currentcamera->GLPrecessInput(m_WindowHandle, 0.1f);
		currentcamera->RenderSkyBox();

		geometrypass->Execute(m_Context, renderResources);
	}
	if (open_Msaa && !open_TAA)
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

	// TAA 按需创建与执行
	if (open_TAA)
	{
		if (!m_TaaFrameBuffers[0])
		{
			m_TaaFrameBuffers[0] = CreatePtr<FrameBuffer>(m_BaseFboSpec);
			m_TaaFrameBuffers[1] = CreatePtr<FrameBuffer>(m_BaseFboSpec);
		}
		if (!m_PrevDepthFrameBuffer)
			m_PrevDepthFrameBuffer = CreatePtr<FrameBuffer>(m_BaseFboSpec);

		int nextFrameIndex = (m_CurrentFrameIndex + 1) % 2;
		m_TaaFrameBuffers[nextFrameIndex]->Bind();
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if (m_TaaShader)
		{
			m_TaaShader->Bind();

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, framebuffer->GetClolorAttachmentRenderID());
			m_TaaShader->SetUniform1i("u_CurrentColor", 0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, m_TaaFrameBuffers[m_CurrentFrameIndex]->GetClolorAttachmentRenderID());
			m_TaaShader->SetUniform1i("u_HistoryColor", 1);

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, renderResources.VelocityTexture);
			m_TaaShader->SetUniform1i("u_VelocityTex", 2);

			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, framebuffer->GetDepthAttachmentRenderID());
			m_TaaShader->SetUniform1i("u_DepthTex", 3);

			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D, m_PrevDepthFrameBuffer->GetDepthAttachmentRenderID());
			m_TaaShader->SetUniform1i("u_HistoryDepthTex", 4);

			m_TaaShader->SetUniformMat4f("u_InverseViewProj", glm::inverse(currentViewProj));
			m_TaaShader->SetUniformMat4f("u_PrevViewProj", m_PrevViewProjMatrix);

			int jitterIndex = m_FrameCount % 16;
			vec2 currentJitter = GetHaltonJitter(jitterIndex);
			vec2 jitterUV = vec2(currentJitter.x / m_ViewPortSize.x, currentJitter.y / m_ViewPortSize.y);
			m_TaaShader->SetUniformVec2("u_JitterUV", { jitterUV.x, jitterUV.y });

			renderer.DrawElement(*m_QuadVA, *m_QuadIB, *m_TaaShader);
		}
		m_TaaFrameBuffers[nextFrameIndex]->UnBind();

		glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer->GetFrameID());
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_PrevDepthFrameBuffer->GetFrameID());
		glBlitFramebuffer(0, 0, m_ViewPortSize.x, m_ViewPortSize.y,
			0, 0, m_ViewPortSize.x, m_ViewPortSize.y,
			GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

		m_PrevViewProjMatrix = currentViewProj;
		m_CurrentFrameIndex = nextFrameIndex;
		m_FrameCount++;
	}
	else
	{
		m_TaaFrameBuffers[0].reset();
		m_TaaFrameBuffers[1].reset();
		m_PrevDepthFrameBuffer.reset();
	}
}

void ExampleLayer::OnImGuiRender()
{
	ShowDockSpace();
	ImGui::Begin(m_HeadTitle.c_str());
	ImGui::Checkbox("OpenMsaa?", &open_Msaa);
	ImGui::Checkbox("OpenTaa?", &open_TAA);
	ImGui::Checkbox("Jitter?", &geometrypass->EnableJitter);
	ImGui::Checkbox("rendew?", &rendertow);
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
		if (m_TaaFrameBuffers[0]) m_TaaFrameBuffers[0]->Rsetsize(m_ViewPortSize);
		if (m_TaaFrameBuffers[1]) m_TaaFrameBuffers[1]->Rsetsize(m_ViewPortSize);
	}


	if(open_TAA && m_TaaFrameBuffers[m_CurrentFrameIndex])
		ImGui::Image((ImTextureID)(uintptr_t)m_TaaFrameBuffers[m_CurrentFrameIndex]->GetClolorAttachmentRenderID(), ImVec2(m_ViewPortSize.x, m_ViewPortSize.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	else
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


vec2 ExampleLayer::GetHaltonJitter(int index)
{
	// 计算 Halton 序列 (Base 2 和 Base 3)
	auto halton = [](int index, int base) -> float {
		float f = 1.0f;
		float r = 0.0f;
		int current = index;
		while (current > 0) {
			f = f / base;
			r = r + f * (current % base);
			current = current / base;
		}
		return r;
		};

	// 返回映射到 [-0.5, 0.5] 的偏移
	return vec2(halton(index + 1, 2) - 0.5f, halton(index + 1, 3) - 0.5f);
}

mat4 ExampleLayer::Jittering(const mat4& originalProj, float width, float height)
{
	// 采用 16 相位的 Halton 序列
	int jitterIndex = m_FrameCount % 16;
	vec2 currentJitter = GetHaltonJitter(jitterIndex);

	// 转换为 NDC 空间偏移 (-1 到 1 的空间，所以乘以 2.0 / 分辨率)
	float deltaX = currentJitter.x * 2.0f / width;
	float deltaY = currentJitter.y * 2.0f / height;

	mat4 jitteredProjMatrix = originalProj;
	jitteredProjMatrix[2][0] += deltaX; // OpenGL 矩阵列主序，修改第三列第一行
	jitteredProjMatrix[2][1] += deltaY; // 修改第三列第二行

	return jitteredProjMatrix;
}
#endif // Drop