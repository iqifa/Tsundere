#pragma once
#include"../Widget.h"
namespace Widget {
	class Select :public Widget {
	private:
		string Lable;
		int LastSelect = -1;
		int Selected;

		virtual void _Draw() override {
			ImGui::LabelText(("##"+Lable).c_str(),Lable.c_str());
		}


	};

	class TCombo :public DiyWidget {
	private:
		string Lable;
		int LastSelect = -1;
		int *Selected;
		using str = char*;
		str* a;
		vector<string>SelectLable;
	public:

		TCombo(string Lable, int* Slected, vector<string>SelectLable, Callback lambda ) :Lable(Lable), Selected(Slected), SelectLable(SelectLable), DiyWidget(lambda) {
			InitSelect();
		}
		virtual void _Draw() override {
			ImGui::PushID(Lable.c_str());
			ImGui::Columns(2);
			ImGui::SetColumnWidth(0,100);
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

		void UpDate(vector<string>SelectLable) {
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
