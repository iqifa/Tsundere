#pragma once
#include"../Core.h"
#include"HeadLine.h"
#include"Event/Event.h"
namespace Engine {
	class T_API Layer
	{
	public:
		Layer(const std::string& name = "Layer");
		virtual ~Layer();

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate() {}
		virtual void OnEvent(Eventing::Event<>& event) { event.Invoke(); }
		virtual void OnRender(){}
		virtual void OnImGuiRender(){}

		inline const std::string& GetName() const { return m_DebugName; }
	protected:
		std::string m_DebugName;
	};

}
