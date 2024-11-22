#include "Application.h"
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
		while (m_Running)
		{
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			
			for (auto layer : layerStack)
			{
				layer->OnUpdate();
			}
			m_iml->Begin();
			for (auto layer : layerStack)
			{
				layer->OnImGuiRender();
			}
			m_iml->End();
			m_Window->OnUpdate();
		}
		
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
