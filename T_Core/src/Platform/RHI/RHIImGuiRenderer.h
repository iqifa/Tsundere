#pragma once
#include "Core/Core.h"
#include "ExternalFiles.h"
#include "Core/Application.h"
#include<Platform/RenderAPI.h>
#include<Debug/Debug.h>
class T_API RHIImGUIRenderer
{
public:
	using InitFunc = void(*)(Engine::Application&);
	using BeginFunc = void(*)(Engine::Application&, float&);
	using EndFunc = void(*)();
	using ShutdownFunc = void(*)();

	struct APIFunctions {
		InitFunc     Init;
		BeginFunc    Begin;
		EndFunc      End;
		ShutdownFunc Shutdown;
	};

	// 核心修改 1：使用 C++17 inline static，直接在头文件完成定义且保证跨 DLL 单例
	inline static APIFunctions s_API = { nullptr, nullptr, nullptr, nullptr };

	static void Register(const APIFunctions& funcs) { s_API = funcs; }

	// 核心修改 2：直接使用 s_API，不再调用 GetAPI()
	static void Init(Engine::Application& app) {
		if (s_API.Init) {
			s_API.Init(app);
			Info_Core("RHIImGUIRenderer Initialized")
		}
		else
			Error_Core("RHIImGUIRenderer Failed Initialized")
	}
	static void Begin(Engine::Application& app, float& m_Time) { if (s_API.Begin) s_API.Begin(app, m_Time); }
	static void End() { if (s_API.End) s_API.End(); }
	static void Shutdown() { if (s_API.Shutdown) s_API.Shutdown(); }
};