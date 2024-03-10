#pragma once
#include"Core/Window.h"
#include"GLHead.h"

namespace Engine {

	class WindowsWindow :public Window {
	public:
		WindowsWindow(const WindowProps& props);
		virtual ~WindowsWindow();

		void OnUpdate() override;

		inline  int GetWidth() const override { return m_Data.Width; }
		inline  int GetHeight() const override { return m_Data.Height; }

		// Window attributes
		inline void SetEventCallback(const CallBack& callback) override { m_Data.callback = callback; }
		void SetVSync(bool enabled) override;
		bool IsVSync() const override;

	private:
		virtual void Init(const WindowProps& props);
		virtual void Shutdown();

		GLFWwindow* m_Window;

		struct WindowData
		{
			std::string Title;
			unsigned int Width, Height;
			bool VSync;
			CallBack callback;
		};

		WindowData m_Data;
	};
}