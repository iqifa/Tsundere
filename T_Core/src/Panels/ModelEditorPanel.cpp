#include "Panels/ModelEditorPanel.h"

ModelEditorPanel*& ModelEditorPanel::Instance()
{
	static ModelEditorPanel* instance = nullptr;
	return instance;
}

Ref<Scene>& ModelEditorPanel::BoundScene()
{
	static Ref<Scene> scene;
	return scene;
}
