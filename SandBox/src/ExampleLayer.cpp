#include "ExampleLayer.h"
#include <Debug/Debug.h>
#include<Trans/SceneCamera.h>
#include<Core/Application.h>
#include<Scene/BVHBuilder.h>


entt::entity m_SelectedContext = null;
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
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Ori/right.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Ori/left.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Ori/top.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Ori/bottom.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Ori/front.jpg",
		"D:\\Code\\C++\\Tsundere\\res/texture/CubeMap/Ori/back.jpg"
	};
	currentcamera->skybox = CreateRef<SkyBox>(texpaths);

	// Create default directional light
	auto lightEntity = scene->CreateEntity("Directional Light");
	lightEntity.AddComponent<Component::DirectionalLight>();

	// Create a few colored point lights for clustered deferred lighting validation
	struct TestPointLight
	{
		const char* Name;
		vec3 Position;
		vec3 Color;
	};
	TestPointLight testLights[] = {
		{ "Point Light Red",   vec3(-3.0f, 2.0f,  0.0f), vec3(1.0f, 0.15f, 0.1f) },
		{ "Point Light Green", vec3( 3.0f, 2.0f,  0.0f), vec3(0.1f, 1.0f, 0.2f) },
		{ "Point Light Blue",  vec3( 0.0f, 1.5f, -3.0f), vec3(0.2f, 0.35f, 1.0f) }
	};
	for (const auto& testLight : testLights)
	{
		auto pointEntity = scene->CreateEntity(testLight.Name);
		auto& transform = pointEntity.GetComponent<Component::Transform>();
		transform.Position = testLight.Position;
		auto& pointLight = pointEntity.AddComponent<Component::PointLight>();
		pointLight.Color = testLight.Color;
		pointLight.Intensity = 25.0f;
		pointLight.Radius = 8.0f;
		pointLight.Falloff = 2.0f;
	}

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

	if (m_EnableRenderGraphTest)
	{
		RunRenderGraphSmokeTest();

		renderResources.SceneColorTexture =
			m_RenderGraphTestOutput;

		return;
	}


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
	ImGui::Checkbox(
		"RenderGraph Smoke Test?",
		&m_EnableRenderGraphTest);
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
	ImGui::Separator();
	if (useDeferred)
	{
		ImGui::Checkbox("Clustered Lights", &deferredLightingPass->ClusteredLightingEnabled);
		ImGui::SliderInt("Cluster Tile Size", &deferredLightingPass->ClusterTileSize, 16, 64);
		ImGui::SliderInt("Cluster Z Slices", &deferredLightingPass->ClusterZSlices, 8, 32);
		ImGui::SliderInt("Max Lights/Cluster", &deferredLightingPass->MaxLightsPerCluster, 16, 128);
		ImGui::Text("Point lights: %d | Clusters: %u x %u x %u",
			deferredLightingPass->GetLocalLightCount(),
			deferredLightingPass->GetClusterCountX(),
			deferredLightingPass->GetClusterCountY(),
			deferredLightingPass->GetClusterCountZ());
	}
	// Expand GBuffer debug modes to include DDGI and clustered lighting
	const char* debugItems2[] = { "Lighting", "Position", "Normal", "Albedo", "Specular", "Depth", "DDGI Irradiance", "DDGI Depth", "Shadow Map", "Cluster Light Count" };
	ImGui::Combo("GBuffer Debug", &deferredLightingPass->DebugMode, debugItems2, 10);
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

void ExampleLayer::RunRenderGraphSmokeTest()
{
	if (m_ViewPortSize.x <= 0.0f || m_ViewPortSize.y <= 0.0f)
		return;

	m_RenderGraphTest.Reset();
	m_RenderGraphTestOutput = 0;

	const uint32_t width =
		static_cast<uint32_t>(m_ViewPortSize.x);
	const uint32_t height =
		static_cast<uint32_t>(m_ViewPortSize.y);

	RDGTextureDesc colorDesc;
	colorDesc.width = width;
	colorDesc.height = height;
	colorDesc.mipLevel = 1;
	colorDesc.arrayLayers = 1;
	colorDesc.format = Format::RGBA8_UNORM;
	colorDesc.usage = TextureUsage::ColorAttachment;

	RGTextureHandle intermediate =
		m_RenderGraphTest.CreateTexture(
			colorDesc,
			"RDG.Smoke.Intermediate");

	RGTextureHandle output =
		m_RenderGraphTest.CreateTexture(
			colorDesc,
			"RDG.Smoke.Output");

	// 使用 shared_ptr，避免 graph 中的 lambda 捕获局部引用。
	auto producerExecuted = std::make_shared<bool>(false);

	m_RenderGraphTest.AddPass(
		"RDG.Smoke.Producer",
		[intermediate](RenderGraphPassBuilder& builder)
		{
			builder.WriteTexture(
				intermediate,
				RGAccess::RenderTarget);
		},
		[intermediate, producerExecuted](
			RHIContext&,
			RenderGraphResources& resources)
		{
			RHITexture2D* texture =
				resources.GetTexture(intermediate);

			unsigned int framebuffer =
				resources.GetFramebuffer({ intermediate });

			if (!texture || !framebuffer)
			{
				Error_Core(
					"[RenderGraph Smoke]: invalid producer resource");
				return;
			}

			glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
			glViewport(
				0,
				0,
				texture->GetWidth(),
				texture->GetHeight());

			glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			*producerExecuted = true;
		});

	m_RenderGraphTest.AddPass(
		"RDG.Smoke.Consumer",
		[intermediate, output](RenderGraphPassBuilder& builder)
		{
			builder.ReadTexture(
				intermediate,
				RGAccess::ReadSRV);

			builder.WriteTexture(
				output,
				RGAccess::RenderTarget);
		},
		[this, intermediate, output, producerExecuted](
			RHIContext&,
			RenderGraphResources& resources)
		{
			RHITexture2D* input =
				resources.GetTexture(intermediate);

			RHITexture2D* outputTexture =
				resources.GetTexture(output);

			unsigned int framebuffer =
				resources.GetFramebuffer({ output });

			if (!input || !outputTexture || !framebuffer)
			{
				Error_Core(
					"[RenderGraph Smoke]: invalid consumer resource");
				return;
			}

			glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
			glViewport(
				0,
				0,
				outputTexture->GetWidth(),
				outputTexture->GetHeight());

			if (*producerExecuted)
			{
				// 绿色：Producer 在 Consumer 前正确执行。
				glClearColor(0.1f, 0.8f, 0.2f, 1.0f);
			}
			else
			{
				// 洋红色：执行顺序错误。
				glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
			}

			glClear(GL_COLOR_BUFFER_BIT);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			m_RenderGraphTestOutput =
				static_cast<unsigned int>(
					outputTexture->GetNativeID());
		});

	m_RenderGraphTest.ExportTexture(output);

	m_RenderGraphTest.Compile();

	Ref<RHIContext> context = RHIRenderer::GetContext();
	if (!context)
	{
		Error_Core(
			"[RenderGraph Smoke]: RHI context is null");
		return;
	}

	m_RenderGraphTest.Execute(*context);
}
#endif // Drop