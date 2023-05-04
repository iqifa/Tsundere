#pragma once
#ifndef GUILAYER
#define GUILAYER

#include"ExternalFiles.h"

class ImGuiLayer
{
public:

	ImGuiLayer();
	~ImGuiLayer() = default;

	virtual void OnAttach();
	virtual void OnDetach();
	//virtual void OnEvent(Event& e);

	void Begin();
	void End();

	void BlockEvent(bool block) {}

};

#endif