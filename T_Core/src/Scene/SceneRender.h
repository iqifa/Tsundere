#pragma once
#ifndef SCENERENDER
#define SCENERENDER

#include"HeadLine.h"
#include"Scene.h"



class  SceneRender
{
public:
	SceneRender(const Ref<Scene>& Scene)
	{
		SetContext(Scene);
	}
	SceneRender() = default;
	~SceneRender() {}
	void SetContext(const Ref<Scene>& Scene)
	{
		m_Context = Scene;
	}
	void OnRender() {
		if (m_Context)
			//DPI has Deprecated ,can refer https://github.com/skypjack/entt/issues/1116
			//m_Context->m_Registry.each([&](auto entityID)
			//	{
			//		Entity entity{ m_Context.get(),entityID };
			//		if (entity.HasComponent<MeshFile>() && entity.HasComponent<Material>())
			//			entity.Draw();
			//	});
			for (auto entityID : m_Context->m_Registry.view<entt::entity>())
			{
				Entity entity{ m_Context.get(),entityID };
				if (entity.HasComponent<MeshFile>() && entity.HasComponent<Material>())
					entity.Draw();
			}
	}

private:

	Ref<Scene> m_Context;
	friend class Scene;
};
#endif