#pragma once
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
#include"Scene/Scene.h"
#include"Panels/Panel.h"
#include"GLHead.h"
#include<Pipeline/RenderPass.h>
#include<Pipeline/RenderGraph.h>
class ExampleLayer :public BasePanel {
public:
public:
	ExampleLayer(Ref<Scene>scene, std::string name = "Example");
private:
	Ref<Scene>m_Context;

	void OnUpdate() override;
	void OnImGuiRender()override;

	void OnEvent(Eventing::Event<>& event) override;
	void DrawEntityNode(Entity entity);

	Ref<RHIShader>shader;

	Ref<RHIFramebuffer>RHI_fb;             // RHI non-MSAA FBO (resolve target / forward rendering)
	Ref<RHIFramebuffer>RHI_msaafb;         // RHI MSAA FBO (16 samples, multi-attachment)

	vec2 m_ViewPortSize;

	bool open_Msaa = false;

	std::deque<Entity>destory;

	GLFWwindow* m_WindowHandle = nullptr;

	Ref<GeometryPass> geometrypass;
	Ref<TAAPass> taaPass;
	Ref<ShadowPass> shadowPass;
	Ref<ShadowApplyPass> shadowApplyPass;
	Ref<ShadowMapPass> shadowMapPass;
	Ref<PathTracePass> pathTracePass;
	RenderResources renderResources;

	Ref<GBufferPass> gbufferPass;
	Ref<DeferredLightingPass> deferredLightingPass;
	Ref<DDGIPass> m_DDGIPass;
	bool useDeferred = true;

	bool m_ViewportFocused = false;


	// --- RenderGraph smoke test ---
	// Validates the graph end to end: texture allocation, attachment binding,
	// framebuffer creation, dependency edges, topological order, export readback.
	RenderGraph m_RenderGraphTest;
	unsigned int m_RenderGraphTestOutput = 0;
	bool m_EnableRenderGraphTest = false;

	RGTextureHandle m_SmokeOutput;
	uint32_t m_SmokeBuiltWidth = 0;
	uint32_t m_SmokeBuiltHeight = 0;

	// Execution order recorded by the pass lambdas, checked after Execute().
	Ref<std::vector<std::string>> m_SmokeOrder;

	void BuildRenderGraphSmokeTest(uint32_t width, uint32_t height);
	void RunRenderGraphSmokeTest();

	// --- RenderGraph forward path ---
	// The same forward chain as the legacy path, expressed as a render graph:
	//   ShadowMap -> Geometry (MRT + depth) -> TAA (imported history)
	// Legacy passes are untouched; this runs instead of them when enabled.
	bool m_UseRenderGraph = false;

	RenderGraph m_RenderGraph;
	Ref<RGFrameData> m_GraphFrameData;
	RGTextureHandle m_GraphFinalColor;
	RGTextureHandle m_GraphVelocity;

	// The graph is structural: it is rebuilt only when the viewport resizes or a
	// toggle changes which passes exist. Every other frame just re-executes it.
	uint32_t m_GraphBuiltWidth = 0;
	uint32_t m_GraphBuiltHeight = 0;
	bool m_GraphDirty = true;

	// Previous toggle values, to detect structural changes.
	bool m_GraphBuiltShadowMap = true;
	bool m_GraphBuiltTAA = true;
	bool m_GraphBuiltDeferred = false;
	bool m_GraphBuiltShadowRay = false;

	void BuildRenderGraph(uint32_t width, uint32_t height);
	void ExecuteRenderGraph();

	// ========================================
	// 新接口测试：RenderGraphPass V2
	// ========================================
	bool m_UseRenderGraphV2 = false;  // 开关：使用新接口
	void BuildRenderGraphV2(uint32_t width, uint32_t height);
	void ExecuteRenderGraphV2();

	// Direction of the first directional light, or a default when the scene has
	// none. ShadowPass needs it set before its lambda runs, and both the forward
	// and deferred graph paths need the same value.
	vec3 SceneLightDir() const;
};