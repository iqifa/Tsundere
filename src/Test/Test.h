#pragma once
#include<iostream>
#include<vector>
#include<functional>
#include"Trans/SceneCamera.h"
using namespace std;

namespace test {
	class Test {

	public:
		Test() {}
		virtual ~Test() {}

		virtual void OnUpdate(float deltaTime) {}
		virtual void OnRender() {}
		virtual void OnImGuiRender() {}
		
	};

	class TestMenu :public Test
	{
	public:
		TestMenu(Test*& currentTest);
		~TestMenu();

		void OnImGuiRender() override;

		template<typename T>
		void RegisterTest(const string& name)
		{
			std::cout << "Registering test" << name << std::endl;

			m_Tests.push_back(make_pair(name, []() {return new T(); }));
		}
		template<typename T>
		void RegisterTest(const string& name,float screenWidth,float screenHeight)
		{
			std::cout << "Registering test" << name << std::endl; 

			m_Tests.push_back(make_pair(name, [screenWidth,screenHeight]() {return new T(screenWidth, screenHeight); }));
		}
	private:
		Test*& m_CurrentTest;
		vector<pair<string, function<Test* ()>>> m_Tests;
	};
}