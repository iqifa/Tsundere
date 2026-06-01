#include "Application.h"
#include "Core/Threading/ResourceLoader.h"
#include "Platform/GL/GLContext.h"
#include "Debug/Debug.h"
namespace Engine {

#define BIND_EVENT_FN(x) std::bind(&Application::x, this, std::placeholders::_1)
	Application* Application::Instance = nullptr;
	Application::Application()
	{
		if (Instance == nullptr)
			Instance = this;
		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(BIND_EVENT_FN(OnEvents));

		m_iml = new ImGuiLayer();

		// Initialize RHI context (GL: wraps GLEW init + frame management)
		// Must be called after window creation (needs the GLFW window handle)
		m_RHIContext = GLContext::Create(static_cast<GLFWwindow*>(m_Window->GetWindow()));
		Info_Core("RHI Context created (OpenGL backend)");
	}

	Application::~Application()
	{
		if (m_RHIContext)
			m_RHIContext->Shutdown();
	}

	void Application::Run()
	{
		// Start async resource loader worker thread
		ResourceLoader::Init();

		while (m_Running)
		{
			// Phase 0: Complete any async resource loads (GPU upload on main thread)
			ResourceLoader::ProcessMainThreadCompletions();

			// Phase 1: Dispatch queued load requests to worker thread
			ResourceLoader::DispatchQueuedLoads();

			// Phase 2: Normal frame — use RHI context for begin/end
			if (m_RHIContext)
				m_RHIContext->BeginFrame();

			for (auto layer : layerStack.GetLayerSnapshot())
			{
				layer->OnUpdate();
			}
			m_iml->Begin();
			for (auto layer : layerStack.GetLayerSnapshot())
			{
				layer->OnImGuiRender();
			}
			m_iml->End();

			if (m_RHIContext)
				m_RHIContext->EndFrame();
			else
				m_Window->OnUpdate();  // fallback if no RHI context
		}

		ResourceLoader::Shutdown();
	}

	void Application::OnEvents(Eventing::Event<>& ev) {
		ev.Invoke();
	}

	void Application::PushLayer(Layer* layer)
	{
		layerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PopLayer(Layer* layer)
	{
		layerStack.PopLayer(layer);
	}
}
