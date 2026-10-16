#include "Application.h"
#include "Core/Threading/ResourceLoader.h"
#include "Core/Assets/GPUDeletionQueue.h"
#include "Platform/RHI/RHIRenderer.h"
#include"Platform/RHI/RHIImGuiRenderer.h"
#include "Platform/RHI/RHIShader.h"
#include "Platform/RHI/RHITextureLibrary.h"
#include "Panels/MeshFilePath.h"
#include "Panels/ModelEditorPanel.h"
#include "Trans/SceneCamera.h"
#include "Debug/Debug.h"
#ifdef RenderAPI_Vulkan
#include "Platform/Vulkan/VulkanDeletionQueue.h"
#endif
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

			// Phase 2: Normal frame. A minimized or out-of-date swapchain does not
			// start a command buffer; skip recording until EndFrame can present.
			RHIRenderer::BeginFrame();
			if (RHIRenderer::IsFrameActive())
			{
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
			}
			RHIRenderer::EndFrame();
		}

		// GPU objects live in layers and static caches. Their destructors must
		// run while the device is still alive, before vkDestroyDevice.
		if (auto context = RHIRenderer::GetContext())
			context->WaitIdle();
#ifdef RenderAPI_Vulkan
		VulkanDeletionQueue::BeginImmediate();
#endif
		RHIImGUIRenderer::Shutdown();
		ResourceLoader::Shutdown();
		layerStack.Clear();
		ModelEditorPanel::BindScene(nullptr);
		My_map::Clear();
		TextureLibiary::Clear();
		ShaderLibiray::Clear();
		if (currentcamera)
			currentcamera->skybox.reset();
		GPUDeletionQueue::Flush();

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
