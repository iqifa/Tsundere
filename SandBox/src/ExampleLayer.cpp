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
	va = CreatePtr<VertexArray>(36);
	float position[] =
	{
	-0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
	 0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
	 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
	 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
	-0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
	-0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

	-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
	 0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
	 0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
	 0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
	-0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
	-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,

	-0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
	-0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
	-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
	-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
	-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
	-0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

	 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
	 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
	 0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
	 0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
	 0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
	 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

	-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
	 0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
	 0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
	 0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
	-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
	-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,

	-0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
	 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
	 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
	 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
	-0.5f,  0.5f,  0.5f,  0.0f, 0.0f,
	-0.5f,  0.5f, -0.5f,  0.0f, 1.0f
	};

	unsigned int indices[] = {
		0,  1,  2,  3,  4,  5,
		6,  7,  8,  9,  10, 11,
		12, 13, 14, 15, 16, 17,
		18, 19, 20, 21, 22, 23,
		24, 25, 26, 27, 28, 29,
		30, 31, 32, 33, 34, 35
	};
	/*vb = CreatePtr<VertexBuffer>(position, 4 * sizeof(float) * 4);*/
	vb = CreatePtr<VertexBuffer>(position, 36 * 5 * sizeof(float));
	ibo = CreatePtr<IndexBuffer>(indices, 36);
	VertexBufferLayout layout;
	layout.Push<float>(3);
	layout.Push<float>(2);
	va->AddBuffer(*vb, layout);

	shader = CreatePtr<Shader>("D:\\Code\\C++\\Tsundere\\res\\shaders\\Basic.shader");


	proj = ortho(0.0f, 1080.0f, 0.0f, 960.0f, -1.0f, 1.0f);
	view = translate(mat4(1.0f), vec3(-100, 0, 0));

	FrameBufferSpecification orispec = { 1080,960,1 };
	FrameBufferSpecification Msaaspec = { 1080,960,16 };
	framebuffer = CreatePtr<FrameBuffer>(orispec);
	Msaaframebuffer = CreatePtr<MsaaFrameBuffer>(Msaaspec);


	// --- 初始化 TAA 需要的 Buffers ---
	m_VelocityFrameBuffer = CreatePtr<FrameBuffer>(orispec);
	m_TaaFrameBuffers[0] = CreatePtr<FrameBuffer>(orispec);
	m_TaaFrameBuffers[1] = CreatePtr<FrameBuffer>(orispec);
	m_PrevDepthFrameBuffer = CreatePtr<FrameBuffer>(orispec); // 新增


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
	m_VelocityShader = CreatePtr<Shader>("D:\\Code\\C++\\Tsundere\\res\\shaders\\Velocity.shader");
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
	if (open_Msaa)
	{
		Msaaframebuffer->Bind();
	}
	else {
		framebuffer->Bind();
	}

	view = currentcamera->GetViewFront();

	//m_UnjitteredProjMatrix = currentcamera->GetProj();
	//mat4 currentViewProj = m_UnjitteredProjMatrix * view; // 干净的 VP 矩阵，给 Velocity 算速度用
	//proj = Jittering(m_UnjitteredProjMatrix, m_ViewPortSize.x, m_ViewPortSize.y);

	proj = currentcamera->GetProj();
	mat4 currentViewProj = proj * view;

	if(open_TAA)
		proj = Jittering(proj, m_ViewPortSize.x, m_ViewPortSize.y);

	model = scale(mat4(1.0f), vec3(1.0f, 2.0f, 1.0f));
	renderer.Clear();
	{
		if (m_ViewportFocused)
			currentcamera->GLPrecessInput(m_WindowHandle, 0.1f);
		currentcamera->RenderSkyBox();
		mat4 mvp_jittered = proj * view ;
		shader->Bind();
		shader->SetUniformMat4f("MVP_matrix", mvp_jittered * model);
		shader->SetUniformVec3("print_color", vec3(0,.5,.3));
		renderer.DrawElement(*va, *ibo, *shader);

		if (rendertow)
		{
			mat4 model2 = scale(mat4(1.0f), vec3(1.0f, 1.0f, 2.0f));
			shader->SetUniformMat4f("MVP_matrix", mvp_jittered * model2);
			shader->SetUniformVec3("print_color", vec3(1, .5, .3));
			renderer.DrawElement(*va, *ibo, *shader);
		}
	}
	if (open_Msaa&&!open_TAA)
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

	glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer->GetFrameID());
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_VelocityFrameBuffer->GetFrameID());
	glBlitFramebuffer(0, 0, m_ViewPortSize.x, m_ViewPortSize.y, 0, 0, m_ViewPortSize.x, m_ViewPortSize.y, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	m_VelocityFrameBuffer->Bind();
	renderer.Clear_Color();


	glDepthFunc(GL_LEQUAL); // 深度小于等于主缓冲才写入速度
	glDepthMask(GL_FALSE);  // 关闭深度写入


	if (m_VelocityShader) {
		m_VelocityShader->Bind();
		m_VelocityShader->SetUniformMat4f("viewProj", currentViewProj); // 必须是干净矩阵
		m_VelocityShader->SetUniformMat4f("prevViewProj", m_PrevViewProjMatrix);

		mat4 currentModel = model;
		mat4 prevModel = model; // 实际引擎中需要保存上一帧的 Model
		m_VelocityShader->SetUniformMat4f("model", currentModel);
		m_VelocityShader->SetUniformMat4f("prevModel", prevModel);

		renderer.DrawElement(*va, *ibo, *m_VelocityShader);
	}

	glDepthMask(GL_TRUE); // 恢复深度状态
	glDepthFunc(GL_LESS);
	m_VelocityFrameBuffer->UnBind();


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
		glBindTexture(GL_TEXTURE_2D, m_VelocityFrameBuffer->GetClolorAttachmentRenderID());
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
	else
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer->GetFrameID());
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_TaaFrameBuffers[nextFrameIndex]->GetFrameID());
		glBlitFramebuffer(0, 0, m_ViewPortSize.x, m_ViewPortSize.y, 0, 0, m_ViewPortSize.x, m_ViewPortSize.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
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

void ExampleLayer::OnImGuiRender()
{
	ShowDockSpace();
	ImGui::Begin(m_HeadTitle.c_str());
	ImGui::Checkbox("OpenMsaa?", &open_Msaa);
	ImGui::Checkbox("OpenTaa?", &open_TAA);
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
		framebuffer->Rsetsize(m_ViewPortSize);
		Msaaframebuffer->Rsetsize(m_ViewPortSize);


		m_VelocityFrameBuffer->Rsetsize(m_ViewPortSize);
		m_TaaFrameBuffers[0]->Rsetsize(m_ViewPortSize);
		m_TaaFrameBuffers[1]->Rsetsize(m_ViewPortSize);
	}


	if(open_TAA)
		ImGui::Image((ImTextureID)(uintptr_t)m_TaaFrameBuffers[m_CurrentFrameIndex]->GetClolorAttachmentRenderID(), ImVec2(m_ViewPortSize.x, m_ViewPortSize.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	else
		ImGui::Image((ImTextureID)(uintptr_t)framebuffer->GetClolorAttachmentRenderID(), ImVec2(m_ViewPortSize.x, m_ViewPortSize.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	
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