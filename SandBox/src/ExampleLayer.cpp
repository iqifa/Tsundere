#include "ExampleLayer.h"
#include <Debug/Debug.h>
#include<Trans/SceneCamera.h>
#include<Core/Application.h>
#include<Scene/BVHBuilder.h>


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
		Error_Core("ExampleLayer: 无法从 Application 获取到窗口句柄！");
	}


	shader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/Basic.shader");



	m_BaseFboSpec = { 1080, 960, 1 };
	framebuffer = CreateRef<FrameBuffer>(m_BaseFboSpec);

	RHI_fb = RHIFramebuffer::Create({ 1080,960,{{Format::RGBA8_UNORM},{Format::RG16F}} });
	RHI_msaafb = RHIFramebuffer::Create({ 1080,960,{{Format::RGBA8_UNORM},{Format::RG16F}},true,16 });

	geometrypass = CreateRef<GeometryPass>();
	geometrypass->Init(framebuffer,RHI_fb);

	gbufferPass = CreateRef<GBufferPass>();
	gbufferPass->Init(framebuffer);

	deferredLightingPass = CreateRef<DeferredLightingPass>();
	deferredLightingPass->Init(framebuffer);

	taaPass = CreateRef<TAAPass>();
	taaPass->Init(framebuffer);
	             
	shadowPass = CreateRef<ShadowPass>();
	shadowPass->Init(framebuffer);

	shadowApplyPass = CreateRef<ShadowApplyPass>();
	shadowApplyPass->Init(framebuffer);

	shadowMapPass = CreateRef<ShadowMapPass>();
	shadowMapPass->Init(framebuffer);

	std::vector<std::string> texpaths{
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Sky2/right.png",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Sky2/left.png",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Sky2/top.png",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Sky2/bottom.png",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Sky2/front.png",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Sky2/back.png"
	};
	currentcamera->skybox = CreateRef<SkyBox>(texpaths);

	// Create default directional light
	auto lightEntity = scene->CreateEntity("Directional Light");
	lightEntity.AddComponent<Component::DirectionalLight>();

	// Build static BVH for ray-traced shadows
	shadowPass->BuildBVH(m_Context);

	pathTracePass = CreateRef<PathTracePass>();
	pathTracePass->Init(framebuffer);
	pathTracePass->SetBVHBuilder(shadowPass->GetBVHBuilder());

		// DDGI probe-based global illumination
		m_DDGIPass = CreateRef<DDGIPass>();
		m_DDGIPass->Init(framebuffer);
		m_DDGIPass->SetBVHBuilder(shadowPass->GetBVHBuilder());
		deferredLightingPass->SetDDGIPass(m_DDGIPass.get());



	// Create test entity with material for UI editing
	//auto testEntity = m_Context->CreateEntity("Test Cube");
	//auto& mr = testEntity.AddComponent<MeshRender>();
	//Ref<Material> mat = CreateRef<Material>("res/shaders/Lit.shader");
	//mr.materials.push_back(mat);
}



void ExampleLayer::OnUpdate()
{
	if (m_ViewPortSize.x <= 0.0f || m_ViewPortSize.y <= 0.0f)
		return;
	Renderer renderer;

	if (pathTracePass->Enabled)
	{
		// Path tracing mode — replace entire rasterization chain
		RHI_fb->Bind();
		renderer.Clear();

		if (m_ViewportFocused)
			currentcamera->GLPrecessInput(m_WindowHandle, 0.1f);

		pathTracePass->Execute(m_Context, renderResources);

		RHI_fb->Unbind();
	}
	else
	{
		if (useDeferred)
		{
			// --- Deferred Rendering Path ---
			if (m_ViewportFocused)
				currentcamera->GLPrecessInput(m_WindowHandle, 0.1f);

			// Shadow Map Pass (depth map from light's perspective)
			if (shadowMapPass)
				shadowMapPass->Execute(m_Context, renderResources);

			// GBuffer pass - outputs Position, Normal, Albedo, Specular, Velocity, Depth
			gbufferPass->Execute(m_Context, renderResources);

			// DDGI probe update (before lighting, uses BVH)
			m_DDGIPass->Execute(m_Context, renderResources);

			// Shadow Pass (ray-traced)
			if (shadowPass->Enabled)
			{
				vec3 lightDir = vec3(-0.5f, -1.0f, -0.5f);
				for (auto entityID : m_Context->m_Registry.view<Component::DirectionalLight>())
				{
					auto& dl = m_Context->m_Registry.get<Component::DirectionalLight>(entityID);
					lightDir = dl.Direction;
					break;
				}
				shadowPass->SetLightDir(lightDir);
				shadowPass->Execute(m_Context, renderResources);
			}

			// Deferred lighting pass - reads GBuffer + shadow, outputs lit scene color (includes skybox)
			deferredLightingPass->Execute(m_Context, renderResources);

			// TAA Pass
			if (taaPass)
				taaPass->Execute(m_Context, renderResources);
		}
		else
		{
			// --- Forward Rendering Path (RHI) ---
			if (m_ViewportFocused)
				currentcamera->GLPrecessInput(m_WindowHandle, 0.5f);

			// Shadow Map Pass (depth map from light's perspective)
			if (shadowMapPass)
				shadowMapPass->Execute(m_Context, renderResources);

			if (open_Msaa)
			{
				RHI_msaafb->Bind();
			}
			else
			{
				RHI_fb->Bind();
			}

			renderer.Clear();
			{
				currentcamera->RenderSkyBox();

				geometrypass->Execute(m_Context, renderResources);
			}
			if (open_Msaa)
			{
				RHI_msaafb->ResolveTo(RHI_fb);
				RHI_msaafb->Unbind();
			}
			else
				RHI_fb->Unbind();

			renderResources.SourceFBO = RHI_fb->GetFramebufferID();

			// Shadow Pass (ray-traced)
			if (shadowPass->Enabled)
			{
				vec3 lightDir = vec3(-0.5f, -1.0f, -0.5f);
				for (auto entityID : m_Context->m_Registry.view<Component::DirectionalLight>())
				{
					auto& dl = m_Context->m_Registry.get<Component::DirectionalLight>(entityID);
					lightDir = dl.Direction;
					break;
				}
				shadowPass->SetLightDir(lightDir);
				shadowPass->Execute(m_Context, renderResources);
			}

			// Apply shadows to scene color
			if (shadowApplyPass)
				shadowApplyPass->Execute(m_Context, renderResources);

			// TAA Pass
			if (taaPass)
				taaPass->Execute(m_Context, renderResources);
		}
	}
}

void ExampleLayer::OnImGuiRender()
{
	ShowDockSpace();

	ImGui::Begin("State");
	ImGui::Checkbox("Deferred Rendering?", &useDeferred);
	ImGui::Checkbox("OpenMsaa?", &open_Msaa);
	if (useDeferred && open_Msaa)
		ImGui::TextColored(ImVec4(1,0.5f,0,1), "MSAA disabled in deferred mode");
	ImGui::Checkbox("TAA?", &taaPass->Enabled);
	if (useDeferred)
		ImGui::Checkbox("Jitter?", &gbufferPass->EnableJitter);
	else
		ImGui::Checkbox("Jitter?", &geometrypass->EnableJitter);
	ImGui::Separator();
	ImGui::Checkbox("Shadow Map (PCF)?", &shadowMapPass->Enabled);
	ImGui::Checkbox("Ray Traced Shadows?", &shadowPass->Enabled);
	ImGui::SliderFloat("Shadow Distance", &shadowPass->LightDistance, 1.0f, 200.0f);
	ImGui::Separator();
	ImGui::Checkbox("Path Trace?", &pathTracePass->Enabled);
	if (pathTracePass->Enabled)
	{
		ImGui::Text("Samples: %u", pathTracePass->GetSampleCount());
		if (ImGui::Button("Reset Accum"))
			pathTracePass->ResetAccumulation();
	}
	ImGui::Separator();
	ImGui::Checkbox("DDGI?", &m_DDGIPass->Enabled);
	if (m_DDGIPass->Enabled)
	{
		ImGui::Checkbox("DDGI in Lighting", &deferredLightingPass->DDGIEnabled);
		int gs[3] = { m_DDGIPass->GridSize.x, m_DDGIPass->GridSize.y, m_DDGIPass->GridSize.z };
		if (ImGui::InputInt3("Grid Size", gs))
			m_DDGIPass->GridSize = { gs[0], gs[1], gs[2] };
		ImGui::SliderFloat("Spacing", &m_DDGIPass->Spacing, 0.5f, 10.0f);
		ImGui::SliderFloat("Probe Radius", &m_DDGIPass->ProbeRadius, 0.5f, 20.0f);
		ImGui::SliderInt("Rays/Probe", &m_DDGIPass->RaysPerProbe, 32, 1024);
		ImGui::SliderInt("Probes/Frame", &m_DDGIPass->ProbesPerUpdate, 8, 256);
		ImGui::SliderFloat("Hysteresis", &m_DDGIPass->Hysteresis, 0.0f, 0.99f);
		ImGui::SliderFloat("Depth Sharpness", &m_DDGIPass->DepthSharpness, 0.0f, 200.0f);
		ImGui::Checkbox("Scroll w/ Camera", &m_DDGIPass->ScrollWithCamera);
			ImGui::Checkbox("Auto Place Grid", &m_DDGIPass->AutoPlaceGrid);
			if (!m_DDGIPass->AutoPlaceGrid)
			{
				float go[3] = { m_DDGIPass->GridOrigin.x, m_DDGIPass->GridOrigin.y, m_DDGIPass->GridOrigin.z };
				if (ImGui::DragFloat3("Grid Origin", go, 0.1f))
					m_DDGIPass->GridOrigin = { go[0], go[1], go[2] };
			}
		ImGui::Checkbox("Show Probes", &m_DDGIPass->ShowProbes);
		ImGui::Text("Probes: %d (updating %d/frame)", m_DDGIPass->GetTotalProbes(), m_DDGIPass->ProbesPerUpdate);
			if (ImGui::Button("Reset DDGI"))
				m_DDGIPass->Reset();
	}
	// Expand GBuffer debug modes to include DDGI
	const char* debugItems2[] = { "Lighting", "Position", "Normal", "Albedo", "Specular", "Depth", "DDGI Irradiance", "DDGI Depth", "Shadow Map" };
	ImGui::Combo("GBuffer Debug", &deferredLightingPass->DebugMode, debugItems2, 9);
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
				BVHBuilder::MarkActiveDirty();
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
		framebuffer->Rsetsize(m_ViewPortSize);
		RHI_fb->Resize(m_ViewPortSize.x, m_ViewPortSize.y);
		RHI_msaafb->Resize(m_ViewPortSize.x, m_ViewPortSize.y);
		if (geometrypass) geometrypass->OnFboResize(m_ViewPortSize.x, m_ViewPortSize.y);
		if (gbufferPass) gbufferPass->OnFboResize(m_ViewPortSize.x, m_ViewPortSize.y);
		if (deferredLightingPass) deferredLightingPass->OnResize(m_ViewPortSize.x, m_ViewPortSize.y);
		if (taaPass) taaPass->OnResize(m_ViewPortSize.x, m_ViewPortSize.y);
		if (shadowPass) shadowPass->OnResize(m_ViewPortSize.x, m_ViewPortSize.y);
		if (shadowApplyPass) shadowApplyPass->OnResize(m_ViewPortSize.x, m_ViewPortSize.y);
		if (pathTracePass) pathTracePass->OnResize(m_ViewPortSize.x, m_ViewPortSize.y);
		if (m_DDGIPass) m_DDGIPass->OnResize(m_ViewPortSize.x, m_ViewPortSize.y);
		currentcamera->SetAspect(m_ViewPortSize.x, m_ViewPortSize.y);
	}


	ImGui::Image((ImTextureID)(uintptr_t)renderResources.SceneColorTexture, ImVec2(m_ViewPortSize.x, m_ViewPortSize.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	
	ImGui::End();
	ImGui::Begin("Velocity");
	ImGui::Image((ImTextureID)(uintptr_t)renderResources.VelocityTexture, ImVec2(m_ViewPortSize.x, m_ViewPortSize.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	ImGui::End();

		// DDGI debug: show irradiance atlas
		if (m_DDGIPass && m_DDGIPass->Enabled)
		{
			ImGui::Begin("DDGI Atlas");
			unsigned int atlasID = m_DDGIPass->GetIrradianceAtlasID();
			if (atlasID)
				ImGui::Image((ImTextureID)(uintptr_t)atlasID, ImVec2(256, 256), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
			ImGui::Text("Probes: %d x %d = %d", m_DDGIPass->GetProbesPerRow(),
				(m_DDGIPass->GetTotalProbes() + m_DDGIPass->GetProbesPerRow() - 1) / m_DDGIPass->GetProbesPerRow(),
				m_DDGIPass->GetTotalProbes());
			ImGui::Text("Grid: %d,%d,%d  Spacing: %.1f  Radius: %.1f",
				m_DDGIPass->GridSize.x, m_DDGIPass->GridSize.y, m_DDGIPass->GridSize.z,
				m_DDGIPass->Spacing, m_DDGIPass->ProbeRadius);
			ImGui::End();
		}

		// Shadow map depth debug visualization
		if (shadowMapPass && renderResources.ShadowMapDepth)
		{
			ImGui::Begin("Shadow Map Depth");
			ImGui::Image((ImTextureID)(uintptr_t)renderResources.ShadowMapDepth,
			             ImVec2(256, 256), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
			ImGui::Text("Light-space depth (D32_SFLOAT, %dx%d)",
			            shadowMapPass->ShadowMapWidth,
			            shadowMapPass->ShadowMapHeight);
			ImGui::End();
		}

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