#pragma once
#ifndef SCENERENDER
#define SCENERENDER

#include"HeadLine.h"
#include"Scene.h"
#include"Pipeline/RenderPass.h"



class  T_API SceneRender
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


	void AddPass(Ref<RenderPass> pass){
		pass->Init();
		m_Passes.push_back(pass);
	}

	void OnRender() {
		if (!m_Context) return;

		unsigned int currentInputTexture = 0;

		for (size_t i = 0; i < m_Passes.size(); i++) {
			Ref<FrameBuffer> targetFBO = GetNextFrameBuffer();
			targetFBO->Bind();
			Renderer::Clear();

			m_Passes[i]->Execute(m_Context, currentInputTexture, targetFBO);

			targetFBO->UnBind();

			currentInputTexture = targetFBO->GetClolorAttachmentRenderID();
		}
		PresentToScreen(currentInputTexture);
	}

private:

	Ref<Scene> m_Context;
	friend class Scene;
	std::vector<Ref<RenderPass>>m_Passes;
};
#endif