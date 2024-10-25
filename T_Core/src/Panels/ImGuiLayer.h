#pragma once
#ifndef GUILAYER
#define GUILAYER

#include"ExternalFiles.h"
#include"Core/Layer/LayerStack.h"
#include"Core/Application.h"
namespace Engine {
	class T_API ImGuiLayer :public Layer
	{
	public:

		ImGuiLayer();
		~ImGuiLayer() = default;

		void OnAttach()override;
		void OnDetach()override;
		void OnUpdate()override;
		//virtual void OnEvent(Event& e);

		void Begin();
		void End();

		void BlockEvent(bool block) {}
	private:
		float m_Time=0.0f;
	};
}

#endif