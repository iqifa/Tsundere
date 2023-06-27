#pragma once
#ifndef WIDGET
#define WIDGET

#include<string>
#include"Event\Event.h"
#include"Event\Event.inl"
#include "../../Core/src/vender/imgui/imgui.h"
#include"../../Core/src/vender/glm/glm.hpp"
#include"../../Core/src/vender/glm/gtc/matrix_integer.hpp"
using namespace std;


namespace Widget
{
	/// <summary>
	/// override _Draw() 
	/// </summary>
	class Widget {
	public:
		virtual void _Draw() = 0;
	};
	/*template<class...Argstype>*/
	class DiyWidget :public Widget {
	public:
		using Callback = std::function<void()>;
		DiyWidget(Callback lambda):m_lambda(lambda){}

		virtual void _Draw() override {
			m_lambda();
		}
	protected:
		Callback m_lambda;
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

	class Separator :public Widget {
	public:
		Separator(string Lable="") :Lable(Lable) {}
		void _Draw() override {
			ImGui::Columns(1);
			ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextAlign, ImVec2{ 0.5f,0.5f });
			ImGui::SeparatorText(Lable.c_str());
			ImGui::PopStyleVar();
		}
	private:
		string Lable;
	};

}
#endif // !WIDGET
