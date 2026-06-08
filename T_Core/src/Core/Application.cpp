#include "Application.h"
#include "Core/Threading/ResourceLoader.h"
#include "Platform/RHI/RHIRenderer.h"
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
	}

	Application::~Application()
	{
	}

	void Application::Run()
	{
		// Initialize RHI (GL: wraps GLEW init + frame management)
		RHIRenderer::Init(static_cast<GLFWwindow*>(m_Window->GetWindow()));
		Info_Core("RHI Renderer initialized (OpenGL backend)");

		// Start async resource loader worker thread
		ResourceLoader::Init();

		while (m_Running)
		{
			// Phase 0: Complete any async resource loads (GPU upload on main thread)
			ResourceLoader::ProcessMainThreadCompletions();

			// Phase 1: Dispatch queued load requests to worker thread
			ResourceLoader::DispatchQueuedLoads();

			// Phase 2: Normal frame
			RHIRenderer::BeginFrame();

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

			RHIRenderer::EndFrame();
		}

		ResourceLoader::Shutdown();
		RHIRenderer::Shutdown();
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
