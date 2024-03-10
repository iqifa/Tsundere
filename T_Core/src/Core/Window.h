#pragma once
#include"Core.h"
#include"Event/Event.h"
#include<sstream>

namespace Engine {
	struct WindowProps
	{
		string Title;
		int Width;
		int Height;

		WindowProps(const string& title = "Tsunder", int width = 1080, int height = 960) :Title(title), Width(width), Height(height)
		{

		}
	};

	

	class T_API Window {
	public:
		using CallBack = std::function<void(Eventing::Event<>&)>;
		virtual ~Window(){}

		virtual void OnUpdate() = 0;

		virtual int GetWidth() const = 0;
		virtual int GetHeight() const = 0;

		virtual void SetEventCallback(const CallBack& callback) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync()const  = 0;

		static Window* Create(const WindowProps& pros = WindowProps());
	};
}