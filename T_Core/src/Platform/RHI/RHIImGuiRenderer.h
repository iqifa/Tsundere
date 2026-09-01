#pragma once
#include "Core/Core.h"
#include "ExternalFiles.h"
#include "Core/Application.h"
#include<Platform/RenderAPI.h>
#include<Debug/Debug.h>
class RHITexture2D;

class T_API RHIImGUIRenderer
{
public:
	using InitFunc = void(*)(Engine::Application&);
	using BeginFunc = void(*)(Engine::Application&, float&);
	using EndFunc = void(*)();
	using ShutdownFunc = void(*)();
	using GetTextureIDFunc = ImTextureID(*)(RHITexture2D*);
	using ReleaseTextureFunc = void(*)(RHITexture2D*);

	struct APIFunctions {
		InitFunc           Init;
		BeginFunc          Begin;
		EndFunc            End;
		ShutdownFunc       Shutdown;
		GetTextureIDFunc   GetTextureID;
		ReleaseTextureFunc ReleaseTexture;
	};

	// �����޸� 1��ʹ�� C++17 inline static��ֱ����ͷ�ļ���ɶ����ұ�֤�� DLL ����
	inline static APIFunctions s_API = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

	static void Register(const APIFunctions& funcs) { s_API = funcs; }

	// �����޸� 2��ֱ��ʹ�� s_API�����ٵ��� GetAPI()
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
	static ImTextureID GetTextureID(RHITexture2D* texture)
	{
		return s_API.GetTextureID ? s_API.GetTextureID(texture) : ImTextureID_Invalid;
	}
	static void ReleaseTexture(RHITexture2D* texture)
	{
		if (s_API.ReleaseTexture)
			s_API.ReleaseTexture(texture);
	}
	static void Shutdown() { if (s_API.Shutdown) s_API.Shutdown(); }
};