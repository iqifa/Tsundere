#include "Layer.h"
namespace Engine {
	Layer::Layer(const std::string& name)
		
	{
		std::cout << name << std::endl;
		m_DebugName = name;
	}

	Layer::~Layer()
	{

	}
}