#include "ExampleLayer.h"
#include <Debug/Debug.h>
#include<Trans/SceneCamera.h>
#include<Core/Application.h>
#include<Scene/BVHBuilder.h>
#include<Pipeline/Passes/GeometryPassV2.h>


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




	RHI_fb = RHIFramebuffer::Create({ 1080,960,{{Format::RGBA8_UNORM},{Format::RG16F}} });
	RHI_msaafb = RHIFramebuffer::Create({ 1080,960,{{Format::RGBA8_UNORM},{Format::RG16F}},true,16 });

	geometrypass = CreateRef<GeometryPass>();
	geometrypass->Init(RHI_fb);

	gbufferPass = CreateRef<GBufferPass>();
	gbufferPass->Init(RHI_fb);

	deferredLightingPass = CreateRef<DeferredLightingPass>();
	deferredLightingPass->Init(RHI_fb);

	taaPass = CreateRef<TAAPass>();
	taaPass->Init(RHI_fb);
	             
	shadowPass = CreateRef<ShadowPass>();
	shadowPass->Init(RHI_fb);

	shadowApplyPass = CreateRef<ShadowApplyPass>();
	shadowApplyPass->Init(RHI_fb);

	shadowMapPass = CreateRef<ShadowMapPass>();
	shadowMapPass->Init(RHI_fb);

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
	pathTracePass->Init(RHI_fb);
	pathTracePass->SetBVHBuilder(shadowPass->GetBVHBuilder());

		// DDGI probe-based global illumination
		m_DDGIPass = CreateRef<DDGIPass>();
		m_DDGIPass->Init(RHI_fb);
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

	// ========================================
	// 新接口测试：RenderGraphPass V2
	// ========================================
	if (m_UseRenderGraphV2)
	{
		ExecuteRenderGraphV2();
		return;
	}

	// RenderGraph forward path. The legacy chain below is untouched and stays
	// reachable by turning this off, so the two can be compared side by side.
	if (m_UseRenderGraph)
	{
		ExecuteRenderGraph();
		return;
	}



	if (pathTracePass->Enabled)
	{
		// Path tracing mode — replace entire rasterization chain
		RHI_fb->Bind();
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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

			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			{
				auto commandBuffer = RHIRenderer::GetCmd();
					if (commandBuffer)
						currentcamera->RenderSkyBox(*commandBuffer);

				geometrypass->Execute(m_Context, renderResources);
			}
			if (open_Msaa)
			{
				RHI_msaafb->ResolveTo(RHI_fb);
				RHI_msaafb->Unbind();
			}
			else
				RHI_fb->Unbind();

			renderResources.SourceFBO = RHI_fb;

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
	

	ImGui::Begin("State");
	ImGui::Checkbox(
		"RenderGraph Smoke Test?",
		&m_EnableRenderGraphTest);
	if (ImGui::Checkbox("Use RenderGraph (forward)?", &m_UseRenderGraph))
		m_GraphDirty = true;
	if (m_UseRenderGraph)
		ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
			"RDG: ShadowMap -> Geometry -> TAA");

	// 新增：V2 接口开关
	if (ImGui::Checkbox("Use RenderGraph V2 (NEW)?", &m_UseRenderGraphV2))
		m_GraphDirty = true;
	if (m_UseRenderGraphV2)
		ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f),
			"RDG V2: Unified Pass Interface (Geometry only)");

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

		// Graph resources are sized at build time, so a resize invalidates them.
		m_GraphDirty = true;
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

// Builds the smoke-test graph. Called on first use and whenever the viewport
// resizes — not every frame. Rebuilding per frame would recreate every physical
// texture and FBO, and would invalidate handles the caller still holds.
// =============================================================================
// RenderGraph forward path
// =============================================================================

// Declares the forward chain into the graph and compiles it.
//
// Called only when the structure changes (resize, or a toggle that adds/removes
// a pass) — not per frame. Handles from a previous build are invalidated by
// Reset(), which is why they are stored as members and refreshed here.
vec3 ExampleLayer::SceneLightDir() const
{
	// Same default as the passes use when no DirectionalLight exists.
	vec3 lightDir = vec3(-0.5f, -1.0f, -0.5f);

	if (!m_Context)
		return lightDir;

	for (auto entityID : m_Context->m_Registry.view<Component::DirectionalLight>())
	{
		lightDir = m_Context->m_Registry.get<Component::DirectionalLight>(entityID).Direction;
		break;
	}

	return lightDir;
}

void ExampleLayer::BuildRenderGraph(uint32_t width, uint32_t height)
{
	m_RenderGraph.Reset();

	if (!m_GraphFrameData)
		m_GraphFrameData = CreateRef<RGFrameData>();

	m_GraphFrameData->ShadowLightViewProj = glm::mat4(1.0f);
	m_GraphFrameData->HasShadowMap = false;
	m_GraphFrameData->ShadowMapDepthID = 0;

	m_GraphFinalColor = {};
	m_GraphVelocity = {};

	// --- ShadowMapPass: depth-only target, no color attachments ---
	RGTextureHandle shadowDepth;
	if (shadowMapPass && shadowMapPass->Enabled)
	{
		shadowDepth = shadowMapPass->AddToGraph(
			m_RenderGraph,
			m_Context,
			m_GraphFrameData);
	}

	// TAA needs all three regardless of which path produced them.
	RGTextureHandle sceneColor;
	RGTextureHandle velocity;
	RGTextureHandle depth;

	if (useDeferred)
	{
		// --- Deferred: GBuffer (5 MRT + depth) -> DeferredLighting ---
		GBufferPass::GraphOutputs gbuffer = gbufferPass->AddToGraph(
			m_RenderGraph,
			m_Context,
			width,
			height);

		// ShadowPass (compute) has to run before lighting samples its mask.
		// Handing the mask handle to DeferredLighting is what creates that
		// ordering: both passes otherwise only *read* gbuffer.Depth, and two
		// readers of one resource produce no edge between them.
		RGTextureHandle shadowMask;
		if (shadowPass && shadowPass->Enabled)
		{
			shadowPass->SetLightDir(SceneLightDir());

			shadowMask = shadowPass->AddToGraph(
				m_RenderGraph,
				m_Context,
				gbuffer.Depth,
				width,
				height);
		}

		sceneColor = deferredLightingPass->AddToGraph(
			m_RenderGraph,
			m_Context,
			m_GraphFrameData,
			gbuffer,
			width,
			height,
			shadowMask);

		velocity = gbuffer.Velocity;
		depth    = gbuffer.Depth;
	}
	else
	{
		// --- Forward: GeometryPass writes color + velocity + depth in one pass ---
		GeometryPass::GraphOutputs geometry = geometrypass->AddToGraph(
			m_RenderGraph,
			m_Context,
			m_GraphFrameData,
			shadowDepth,
			width,
			height);

		sceneColor = geometry.SceneColor;
		velocity   = geometry.Velocity;
		depth      = geometry.Depth;
	}

	m_GraphVelocity = velocity;

	// --- ShadowPass (compute) + ShadowApply, forward only for now ---
	//
	// ShadowPass is a compute pass writing an R8 storage image; ShadowApply then
	// composites it over the scene color. The read of the mask by ShadowApply is
	// what orders the two.
	//
	// Deferred does not come through here: there the mask is consumed by
	// DeferredLighting instead of a separate composite, so ShadowPass is added
	// inside the deferred branch above and its handle passed straight in.
	if (!useDeferred && shadowPass && shadowPass->Enabled && shadowApplyPass)
	{
		// Light direction is CPU state read at execute time by Execute(); the
		// graph path needs it set before the pass runs.
		shadowPass->SetLightDir(SceneLightDir());

		RGTextureHandle shadowMask = shadowPass->AddToGraph(
			m_RenderGraph,
			m_Context,
			depth,
			width,
			height);

		if (shadowMask.id != InvalidResourceId)
		{
			sceneColor = shadowApplyPass->AddToGraph(
				m_RenderGraph,
				sceneColor,
				shadowMask,
				width,
				height);
		}
	}

	// --- TAAPass: imported cross-frame history + copy-back ---
	if (taaPass && taaPass->Enabled)
	{
		sceneColor = taaPass->AddToGraph(
			m_RenderGraph,
			sceneColor,
			velocity,
			depth,
			width,
			height);
	}

	m_GraphFinalColor = sceneColor;

	// Both are read back by ImGui after execution, so they must outlive the graph
	// run and must not be aliased onto other resources later.
	m_RenderGraph.ExportTexture(m_GraphFinalColor);
	m_RenderGraph.ExportTexture(m_GraphVelocity);

	m_RenderGraph.Compile();

	m_GraphBuiltWidth = width;
	m_GraphBuiltHeight = height;
	m_GraphBuiltShadowMap = shadowMapPass ? shadowMapPass->Enabled : false;
	m_GraphBuiltTAA = taaPass ? taaPass->Enabled : false;
	m_GraphBuiltDeferred = useDeferred;
	m_GraphBuiltShadowRay = shadowPass ? shadowPass->Enabled : false;
	m_GraphDirty = false;
}

void ExampleLayer::ExecuteRenderGraph()
{
	const uint32_t width = static_cast<uint32_t>(m_ViewPortSize.x);
	const uint32_t height = static_cast<uint32_t>(m_ViewPortSize.y);

	if (width == 0 || height == 0)
		return;

	// Rebuild when the structure changes: viewport size, or a toggle that adds
	// or removes a pass. A value-only change (jitter, light direction) is read
	// inside the pass lambdas each frame and needs no rebuild.
	const bool shadowMapEnabled = shadowMapPass ? shadowMapPass->Enabled : false;
	const bool taaEnabled = taaPass ? taaPass->Enabled : false;
	const bool shadowRayEnabled = shadowPass ? shadowPass->Enabled : false;

	if (m_GraphDirty
		|| width != m_GraphBuiltWidth
		|| height != m_GraphBuiltHeight
		|| shadowMapEnabled != m_GraphBuiltShadowMap
		|| taaEnabled != m_GraphBuiltTAA
		|| useDeferred != m_GraphBuiltDeferred
		|| shadowRayEnabled != m_GraphBuiltShadowRay)
	{
		BuildRenderGraph(width, height);
	}

	if (m_ViewportFocused)
		currentcamera->GLPrecessInput(m_WindowHandle, 0.5f);

	Ref<RHIContext> context = RHIRenderer::GetContext();
	if (!context)
	{
		Error_Core("[RenderGraph]: RHI context is null");
		return;
	}

	// DDGI runs outside the graph, before it. It reads no graph-owned resource
	// (only the scene + BVH) and writes its own ping-ponged atlases, so it needs
	// no dependency edge. The atlas it exposes changes every frame, which is why
	// the IDs travel through RGFrameData and are read at execute time instead of
	// being captured when the graph was built.
	if (useDeferred && m_DDGIPass && m_DDGIPass->Enabled)
	{
		m_DDGIPass->Execute(m_Context, renderResources);

		if (m_GraphFrameData)
		{
			m_GraphFrameData->DDGIIrradianceAtlasID = renderResources.DDGIIrradianceAtlas;
			m_GraphFrameData->DDGIDepthAtlasID      = renderResources.DDGIDepthAtlas;
		}
	}
	else if (m_GraphFrameData)
	{
		m_GraphFrameData->DDGIIrradianceAtlasID = 0;
		m_GraphFrameData->DDGIDepthAtlasID      = 0;
	}

	// Ray-traced ShadowPass reads the graph-owned depth texture, so unlike DDGI it
	// cannot be hoisted out of the graph. That needs stage 7; until then deferred
	// lighting runs without a shadow mask, which Execute() already handles as 0.
	if (m_GraphFrameData)
		m_GraphFrameData->ShadowMaskID = 0;

	m_RenderGraph.Execute(*context);

	// Read the exported results after execution. Ordinary transient resources
	// have no guaranteed contents here, which is why these two were exported.
	RHITexture2D* finalColor = m_RenderGraph.GetExportedTexture(m_GraphFinalColor);
	renderResources.SceneColorTexture = finalColor
		? static_cast<unsigned int>(finalColor->GetNativeID())
		: 0;

	RHITexture2D* velocity = m_RenderGraph.GetExportedTexture(m_GraphVelocity);
	renderResources.VelocityTexture = velocity
		? static_cast<unsigned int>(velocity->GetNativeID())
		: 0;

	// Keep the shadow-map debug view working in graph mode.
	if (m_GraphFrameData)
		renderResources.ShadowMapDepth = m_GraphFrameData->ShadowMapDepthID;
}

void ExampleLayer::BuildRenderGraphSmokeTest(uint32_t width, uint32_t height)
{
	m_RenderGraphTest.Reset();
	m_RenderGraphTestOutput = 0;

	m_SmokeOrder = CreateRef<std::vector<std::string>>();

	RDGTextureDesc colorDesc;
	colorDesc.width  = width;
	colorDesc.height = height;
	colorDesc.format = Format::RGBA8_UNORM;
	colorDesc.usage  = TextureUsage::ColorAttachment;

	RGTextureHandle intermediate =
		m_RenderGraphTest.CreateTexture(colorDesc, "RDG.Smoke.Intermediate");

	RGTextureHandle output =
		m_RenderGraphTest.CreateTexture(colorDesc, "RDG.Smoke.Output");

	// Producer: clears Intermediate to red. Declared second in the graph on
	// purpose is NOT what happens here — it is declared first, but the ordering
	// that matters is proven by Consumer's read dependency below.
	auto order = m_SmokeOrder;

	m_RenderGraphTest.AddPass(
		"RDG.Smoke.Producer",
		[intermediate](RenderGraphPassBuilder& builder)
		{
			// Attachment registers the write itself — no WriteTexture() needed.
			builder.SetColorAttachment(
				0,
				intermediate,
				RGLoadOp::Clear,
				{ 1.0f, 0.0f, 0.0f, 1.0f });
		},
		[order](RHICommandBuffer&, RenderGraphResources&)
		{
			// The graph already bound the framebuffer, set the viewport and
			// cleared to red. Nothing for the pass body to do but record that
			// it ran.
			order->push_back("Producer");
		});

	// Consumer: reads Intermediate, writes Output. The read is what forces
	// Producer to execute first.
	m_RenderGraphTest.AddPass(
		"RDG.Smoke.Consumer",
		[intermediate, output](RenderGraphPassBuilder& builder)
		{
			builder.ReadTexture(intermediate, RGAccess::ReadSRV);

			builder.SetColorAttachment(
				0,
				output,
				RGLoadOp::Clear,
				{ 0.1f, 0.8f, 0.2f, 1.0f });   // green
		},
		[order, intermediate](RHICommandBuffer&, RenderGraphResources& resources)
		{
			// Resolving the input proves handle lookup works for a resource
			// produced by an earlier pass.
			if (!resources.GetTexture(intermediate))
				Error_Core("[RDG Smoke]: Consumer could not resolve Intermediate");

			order->push_back("Consumer");
		});

	m_RenderGraphTest.ExportTexture(output);
	m_RenderGraphTest.Compile();

	m_SmokeOutput       = output;
	m_SmokeBuiltWidth   = width;
	m_SmokeBuiltHeight  = height;
}

void ExampleLayer::RunRenderGraphSmokeTest()
{
	if (m_ViewPortSize.x <= 0.0f || m_ViewPortSize.y <= 0.0f)
		return;

	const uint32_t width  = static_cast<uint32_t>(m_ViewPortSize.x);
	const uint32_t height = static_cast<uint32_t>(m_ViewPortSize.y);

	if (width != m_SmokeBuiltWidth || height != m_SmokeBuiltHeight)
		BuildRenderGraphSmokeTest(width, height);

	Ref<RHIContext> context = RHIRenderer::GetContext();
	if (!context)
	{
		Error_Core("[RDG Smoke]: RHI context is null");
		return;
	}

	if (m_SmokeOrder)
		m_SmokeOrder->clear();

	m_RenderGraphTest.Execute(*context);

	// Verify the dependency actually ordered the passes. Producer must run
	// first because Consumer declared a read of what Producer writes.
	if (m_SmokeOrder)
	{
		const bool ordered =
			m_SmokeOrder->size() == 2
			&& (*m_SmokeOrder)[0] == "Producer"
			&& (*m_SmokeOrder)[1] == "Consumer";

		if (!ordered)
		{
			static bool reported = false;
			if (!reported)
			{
				Error_Core("[RDG Smoke]: wrong execution order, {} pass(es) ran",
					m_SmokeOrder->size());
				reported = true;
			}
		}
	}

	// Read the output from outside the graph. A pass lambda should not be
	// responsible for handing results to the UI.
	RHITexture2D* outputTexture =
		m_RenderGraphTest.GetExportedTexture(m_SmokeOutput);

	m_RenderGraphTestOutput = outputTexture
		? static_cast<unsigned int>(outputTexture->GetNativeID())
		: 0;
}

// ============================================================================
// 新接口实现：RenderGraphPass V2
// ============================================================================

void ExampleLayer::BuildRenderGraphV2(uint32_t width, uint32_t height)
{
	m_RenderGraph.Reset();

	if (!m_GraphFrameData)
		m_GraphFrameData = CreateRef<RGFrameData>();

	// ========================================
	// 1. 注册所有 Pass（顺序无关紧要！）
	// ========================================

	// Geometry Pass（新接口）
	auto geometryPassV2 = CreateRef<GeometryPassV2>(m_Context, m_GraphFrameData);
	geometryPassV2->EnableJitter = geometrypass->EnableJitter;  // 复制设置
	geometryPassV2->SetViewportSize(width, height);
	m_RenderGraph.AddPass(geometryPassV2);

	// TODO: 添加更多 Pass（ShadowMap, TAA 等）
	// auto shadowMapPass = CreateRef<ShadowMapPassV2>(...);
	// m_RenderGraph.AddPass(shadowMapPass);

	// ========================================
	// 2. Compile：自动排序 + 分配资源
	// ========================================
	m_RenderGraph.Compile();

	// ========================================
	// 3. 获取导出的资源（用于 ImGui 显示）
	// ========================================
	m_GraphFinalColor = m_RenderGraph.GetTextureByName("Geometry.SceneColor");
	m_GraphVelocity = m_RenderGraph.GetTextureByName("Geometry.Velocity");

	m_GraphBuiltWidth = width;
	m_GraphBuiltHeight = height;
	m_GraphDirty = false;

	Info_Core("[RenderGraph V2]: Built graph with {} passes", 1);
}

void ExampleLayer::ExecuteRenderGraphV2()
{
	const uint32_t width = static_cast<uint32_t>(m_ViewPortSize.x);
	const uint32_t height = static_cast<uint32_t>(m_ViewPortSize.y);

	if (width == 0 || height == 0)
		return;

	// Rebuild when viewport size changes
	if (m_GraphDirty || width != m_GraphBuiltWidth || height != m_GraphBuiltHeight)
	{
		BuildRenderGraphV2(width, height);
	}

	if (m_ViewportFocused)
		currentcamera->GLPrecessInput(m_WindowHandle, 0.5f);

	Ref<RHIContext> context = RHIRenderer::GetContext();
	if (!context)
	{
		Error_Core("[RenderGraph V2]: RHI context is null");
		return;
	}

	// 一行搞定！
	m_RenderGraph.Execute(*context);

	// 读取已显式导出的结果。不要从 ExampleLayer 访问 RenderGraph 的私有资源解析接口。
	RHITexture2D* finalColor = m_RenderGraph.GetExportedTexture(m_GraphFinalColor);
	renderResources.SceneColorTexture = finalColor
		? static_cast<unsigned int>(finalColor->GetNativeID())
		: 0;

	RHITexture2D* velocity = m_RenderGraph.GetExportedTexture(m_GraphVelocity);
	renderResources.VelocityTexture = velocity
		? static_cast<unsigned int>(velocity->GetNativeID())
		: 0;
}
#endif // Drop