#include "VulkanWindow.h"
#include "Platform/RenderAPI.h"

namespace Engine{

	static bool s_GLFWInitialized = false;
#ifdef RenderAPI_Vulkan
	Window* Window::Create(const  WindowProps& props)
	{
		return new VulkanWindow(props);
	}
#endif

	VulkanWindow::VulkanWindow(const WindowProps& props)
	{
		Init(props);
	}

	VulkanWindow::~VulkanWindow()
	{
	}

	void VulkanWindow::OnUpdate()
	{
	}

	void VulkanWindow::SetVSync(bool enabled)
	{
	}

	bool VulkanWindow::IsVSync() const
	{
		return false;
	}

	void VulkanWindow::Init(const WindowProps& props)
	{
		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		Info_Core("Creating window {0} ({1}, {2} For Vulkan)", props.Title, props.Width, props.Height);


		if (!s_GLFWInitialized)
		{
			// TODO: glfwTerminate on system shutdown
			int success = glfwInit();
			if (!success)
			{
				Error_Core("Could not intialize GLFW!")
			}
			s_GLFWInitialized = true;
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);


		if (!glfwVulkanSupported())
		{
			Error_Core("Vulkan is not supported by GLFW on this system!");
		}

		glfwSetWindowUserPointer(m_Window, &m_Data);

		m_Data.VSync = true;


		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			data.Width = width;
			data.Height = height;
			Eventing::Event<> ev1;
			ev1 += [&]() {
				Info_Core("MyEvent:Width {0},Height {1}", width, height)
				};
			data.callback(ev1);
			});
	}

	void VulkanWindow::Shutdown()
	{
	}
}
