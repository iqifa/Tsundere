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
		m_Context->m_Registry.each([&](auto entityID)
			{
				Entity entity{ m_Context.get(),entityID };
				entity.Draw();
			});
	}

private:

	Ref<Scene> m_Context;

	friend class Scene;
};

#endif