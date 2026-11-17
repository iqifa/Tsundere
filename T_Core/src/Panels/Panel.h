#pragma once
#ifndef PANEL
#define PANEL



#include"Core/Layer/Layer.h"
#include"Scene/Component.h"
#include <Widget.h>
using namespace Component;
extern 	entt::entity m_SelectedContext;

class BasePanel :public Engine::Layer{
public:
	std::vector<Widget::Widget*> widgets;

	BasePanel() = default;
	BasePanel(std::string Title) :Layer(Title) { m_HeadTitle = Title; }

	virtual void _Draw_Wdigets()
	{
		for (auto& widget : widgets)
		{
			widget->_Draw();
		}
	}

	virtual void SetHeadTile(std::string Title) { m_HeadTitle = Title; };

	std::string m_HeadTitle;
};
#endif // !PANEL