#pragma once
#include"Core/Window.h"
#include"ExternalFiles.h"
#include"Debug/Debug.h"
namespace Engine {
	class VulkanWindow :public Window {
	public:
		VulkanWindow(const WindowProps& props);
		virtual ~VulkanWindow();

		void OnUpdate() override;

		inline  int GetWidth() const override { return m_Data.Width; }
		inline  int GetHeight() const override { return m_Data.Height; }

		virtual void* GetWindow() const override { return m_Window; }
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