#include "Layer.h"
#include<Debug/Debug.h>
namespace Engine {
	Layer::Layer(const std::string& name)
	{
		Info_Core("Panel:" + name+" Pushed")
		m_DebugName = name;
	}

	Layer::~Layer()
	{

	}
}