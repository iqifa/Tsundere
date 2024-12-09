#pragma once
#include"../Widget.h"
#include"imgui/imgui.h"
#include"../../T_Core/src/HeadLine.h"
#include"../../T_Core/src/GLHead.h"
namespace Widget {
	class Select :public Widget {
	private:
		std::string Lable;
		int LastSelect = -1;
		int Selected;
		ImVec2 size = ImVec2(50, 50);
		Ref<Texture> image;
		virtual void _Draw() override {

			ImGui::PushID(Lable.c_str());

			ImGuiWindow* window = ImGui::GetCurrentWindow();
			const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
			ImGui::Selectable((Lable + "_Select").c_str(), &Selected, ImGuiSelectableFlags_None, size);
			ImGui::LabelText((Lable + "_Lable").c_str(), Lable.c_str());
			window->DrawList->AddImage((void*)image->GetTextureID(), bb.Min, bb.Max);

			ImGui::PopID();
		}

	public:
		Select(std::string lable)
		{
			Lable = lable;
		}
	};

	class Image_Select :public Widget {
	private:
		std::string Lable;
		bool Selected = false;
		ImVec2 size;
		Ref<Texture> image;
		virtual void _Draw()override {
			ImGui::PushID(Lable.c_str());


			ImGuiWindow* window = ImGui::GetCurrentWindow();
			const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);

			ImGui::Selectable((Lable + "_Select").c_str(), &Selected, ImGuiSelectableFlags_None, size);
			ImGui::LabelText((Lable + "_Lable").c_str(), Lable.c_str());
			window->DrawList->AddImage((void*)image->GetTextureID(), bb.Min, bb.Max, ImVec2{ 0,1 }, ImVec2{ 1,0 });

			ImGui::PopID();
		}
	public:
		Image_Select(std::string lable,ImVec2 size, Ref<Texture> image) {
			Lable = lable;
			this->size = size;
			this->image = image;
		}
	};

	class TCombo :public DiyWidget {
	private:
		std::string Lable;
		int LastSelect = -1;
		int* Selected;
		using str = char*;
		str* a;
		std::vector<std::string>SelectLable;
	public:

		TCombo(std::string Lable, int* Slected, std::vector<std::string>SelectLable, Callback lambda) :Lable(Lable), Selected(Slected), SelectLable(SelectLable), DiyWidget(lambda) {
			InitSelect();
		}
		virtual void _Draw() override {
			ImGui::PushID(Lable.c_str());
			ImGui::Columns(2);
			ImGui::SetColumnWidth(0, 100);
			ImGui::LabelText(("##" + Lable).c_str(), Lable.c_str());
			ImGui::SameLine();
			ImGui::NextColumn();
			ImGui::Combo(("##" + Lable).c_str(), Selected, a, SelectLable.size());
			if (LastSelect != *Selected)
			{
				m_lambda();
				LastSelect = *Selected;
			}
			ImGui::PopID();
			ImGui::Columns();
			ImGui::Separator();
		}

		void UpDate(std::vector<std::string>SelectLable) {
			this->SelectLable = SelectLable;
			InitSelect();
		}
	private:
		void InitSelect() {

			free(a);
			a = new str[SelectLable.size()];
			for (int i = 0; i < SelectLable.size(); i++)
			{
				a[i] = (str)SelectLable[i].c_str();
			}
		}
	};
}
