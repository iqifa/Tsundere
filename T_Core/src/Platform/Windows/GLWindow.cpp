#include"GLWindow.h"
#include"Debug/Debug.h"

namespace Engine {
	static bool s_GLFWInitialized = false;

#ifdef RenderAPI_OpenGL
	Window* Window::Create(const  WindowProps& props)
	{
		return new GLWindow(props);
	}
#endif

	GLWindow::GLWindow(const WindowProps& props)
	{
		Init(props);
	}
	GLWindow::~GLWindow()
	{
		Shutdown();
	}
	void GLWindow::OnUpdate()
	{
		//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glfwPollEvents();
		glfwSwapBuffers(m_Window);
	}
	void GLWindow::SetVSync(bool enabled)
	{
		if (enabled)
			glfwSwapInterval(1);
		else
			glfwSwapInterval(0);

		m_Data.VSync = enabled;
	}
	bool GLWindow::IsVSync() const
	{
		return m_Data.VSync;
	}
	void GLWindow::Init(const WindowProps& props)
	{
		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		Info_Core("Creating window {0} ({1}, {2})", props.Title, props.Width, props.Height);

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

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);


		glfwMakeContextCurrent(m_Window);

		glEnable(GL_DEPTH_TEST);
		glewInit();


		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(true);
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
	void GLWindow::Shutdown()
	{
		glfwDestroyWindow(m_Window);
	}
}