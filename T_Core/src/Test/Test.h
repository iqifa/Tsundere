#pragma once
#include<iostream>
#include<vector>
#include<functional>
#include"../Trans/SceneCamera.h"
#include "../Platform/GL/Renderer.h"
#include"../HeadLine.h"
#include"../GLHead.h"
using namespace std;

namespace test {
	class Test {

	public:
		Test() {}
		virtual ~Test() {}

		virtual void OnUpdate(float deltaTime) {}
		virtual void OnRender() { 
			if (currentcamera)
				GLCall(currentcamera->RenderSkyBox());
			RenderCall();
		}
		virtual void OnImGuiRender() {}
		virtual void RenderCall(){}
		virtual Ref<FrameBuffer>& getfb() { return fb; }
	private:
		Ref<FrameBuffer> fb;
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
		template<typename T,typename ...Args>
		void RegisterTest(const string& name,Args&& ...args )
		{
			std::cout << "Registering test" << name << std::endl;

			m_Tests.push_back(make_pair(name, [&,args...]() {return new T(args...);  }));
		}
	private:
		Test*& m_CurrentTest;
		vector<pair<string, function<Test* ()>>> m_Tests;
	};
}