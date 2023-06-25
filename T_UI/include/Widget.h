#pragma once


#include<string>
#include"Event\Event.h"
#include"Event\Event.inl"
using namespace std;
#ifndef WIDGET
#define WIDGET

namespace Widget
{
	bool SelectButton(ImTextureID img, ImVec2 size, bool selected)
	{

		if (selected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(255, 255, 255, 255));
		}
		else
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(255, 255, 0, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(255, 255, 0, 255));
		bool rec = ImGui::ImageButton(img, size, ImVec2{ 0,1 }, ImVec2{ 1,0 });

		ImGui::PopStyleColor(2);

		return rec;
	}
	class Widget {
	public:
		virtual void _Draw() = 0;
	};
	class Button:public	Widget {
	public:
		Eventing::Event<> ClickEvents;
		string lable;
	};
	class  ImageRadioButton :public Button
	{
	public:
		ImageRadioButton(ImTextureID img, ImVec2 size, bool select = false):img_id(img),size(size),selected(select){}

		virtual void _Draw() override {
			if (selected)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(255, 255, 255, 255));
			}
			else
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(255, 255, 0, 255));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(255, 255, 0, 255));
			if (ImGui::ImageButton(img_id, size, ImVec2{ 0,1 }, ImVec2{ 1,0 }))
			{
				ClickEvents.Invoke();
			}

			ImGui::PopStyleColor(2);
		}

	private:
		ImTextureID img_id;
		ImVec2 size;
		bool selected = false;
	};

}
#endif // !WIDGET
