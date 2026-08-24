#include "Application.h"
#include "Core/Threading/ResourceLoader.h"
#include "Core/Assets/GPUDeletionQueue.h"
#include "Platform/RHI/RHIRenderer.h"
#include"Platform/RHI/RHIImGuiRenderer.h"
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

	}

	Application::~Application()
	{
	}

	void Application::Run()
	{
		// Initialize RHI (GL: wraps GLEW init + frame management)
		RHIRenderer::Init(static_cast<GLFWwindow*>(m_Window->GetWindow()));

		RHIImGUIRenderer::Init(*this);


		// Start async resource loader worker thread
		ResourceLoader::Init();

		while (m_Running)
		{
			// Phase 0: Complete any async resource loads (GPU upload on main thread)
			ResourceLoader::ProcessMainThreadCompletions();

			// Phase 0.5: Flush deferred GL deletions (safe thread-agnostic GPU resource cleanup)
			GPUDeletionQueue::Flush();

			// Phase 1: Dispatch queued load requests to worker thread
			ResourceLoader::DispatchQueuedLoads();

			// Phase 2: Normal frame
			RHIRenderer::BeginFrame();

			for (auto layer : layerStack.GetLayerSnapshot())
			{
				layer->OnUpdate();
			}
			RHIImGUIRenderer::Begin(*this, m_Time);
			for (auto layer : layerStack.GetLayerSnapshot())
			{
				layer->OnImGuiRender();
			}
			RHIImGUIRenderer::End();
			RHIRenderer::EndFrame();
		}

		ResourceLoader::Shutdown();
		RHIImGUIRenderer::Shutdown();
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
