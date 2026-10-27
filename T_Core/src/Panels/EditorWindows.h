#pragma once
#include "Core/Core.h"

// One copy, owned by T_Core. Inline statics in this header would be duplicated
// in the DLL and the executable, so the menu and the windows would not see
// each other's open/close.
struct T_API EditorWindows
{
	static bool& ShadowMap();
	static bool& ModelImport();
	static bool& Console();
};
