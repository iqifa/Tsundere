#pragma once
#include"Event/Event.h"
#include"Panels/ImGuiLayer.h"
#include"Scene/Scene.h"
#include"Panels/Panel.h"
#include"GLHead.h"
#include<Pipeline/RenderPass.h>
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

	Ptr<VertexArray>va;
	Ptr<VertexBuffer>vb;
	Ptr<IndexBuffer>ibo;
	Ref<RHIShader>shader;

	Ref<FrameBuffer> framebuffer;           // legacy — used by non-migrated passes & path tracing

	Ref<RHIFramebuffer>RHI_fb;             // RHI non-MSAA FBO (resolve target / forward rendering)
	Ref<RHIFramebuffer>RHI_msaafb;         // RHI MSAA FBO (16 samples, multi-attachment)

	vec2 m_ViewPortSize;
	FrameBufferSpecification m_BaseFboSpec;

	bool open_Msaa = false;

	std::deque<Entity>destory;

	GLFWwindow* m_WindowHandle = nullptr;

	Ref<GeometryPass> geometrypass;
	Ref<TAAPass> taaPass;
	Ref<ShadowPass> shadowPass;
	Ref<ShadowApplyPass> shadowApplyPass;
	Ref<PathTracePass> pathTracePass;
	RenderResources renderResources;

	Ref<GBufferPass> gbufferPass;
	Ref<DeferredLightingPass> deferredLightingPass;
	Ref<DDGIPass> m_DDGIPass;
	bool useDeferred = false;

	bool m_ViewportFocused = false;
};