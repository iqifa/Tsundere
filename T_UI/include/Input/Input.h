#pragma once
#ifndef INPUT
#define INPUT
#include"Widget.h"

using namespace glm;
namespace Widget {
	template<class T>
	class __declspec(dllexport) Input :public Widget
	{
	public:
		Input(std::string lable, unsigned int value) :Name(lable), value(value),Lable("##"+ lable) {
			
		}
		virtual void _Draw() override {
			ImGui::Columns(2);
			ImGui::SetColumnWidth(0,100);
			ImGui::LabelText(Lable.c_str(), Name.c_str());
			ImGui::NextColumn();
			Draw<T>();
			ImGui::Columns();
		}
		void fun();
		void fun2();
	private:
		std::string Name;
		std::string Lable;
		unsigned int value;


		template<typename T2>
		void Draw()
		{
			/*debugerror("Error!:don't set var type");*/
		}
		template<>
		void Draw<int>()
		{
			int* var = (int*)value;
			ImGui::InputInt(Lable.c_str(), var);
		}
		template<>
		void Draw<float>()
		{
			float* var = (float*)value;
			ImGui::InputFloat(Lable.c_str(), var);
		}
		template<>
		void Draw<std::string>()
		{
			string* var = (string*)value;
			char* str = var->data();
			ImGui::InputText(Lable.c_str(), str, 100);
			*var = str;
		}
		template<>
		void Draw<vec2>()
		{
			vec2* var = (vec2*)value;
			float* aa = (float*)var;
			ImGui::InputFloat2(Lable.c_str(), aa);
		}
		template<>
		void Draw<vec3>()
		{
			vec3* var = (vec3*)value;
			float* aa = (float*)var;
			ImGui::InputFloat3(Lable.c_str(), aa);
		}
		template<>
		void Draw<double>()
		{
			double* var = (double*)value;
			ImGui::InputDouble(Lable.c_str(), var);
		}
	};
}
#endif // !INPUT


