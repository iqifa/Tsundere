#include "Application.h"
#include "Core/Threading/ResourceLoader.h"
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
		// Start async resource loader worker thread
		ResourceLoader::Init();

		while (m_Running)
		{
			// Phase 0: Complete any async resource loads (GPU upload on main thread)
			ResourceLoader::ProcessMainThreadCompletions();

			// Phase 1: Dispatch queued load requests to worker thread
			ResourceLoader::DispatchQueuedLoads();

			// Phase 2: Normal frame
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
			m_Window->OnUpdate();
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
