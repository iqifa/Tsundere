#include "Layer.h"
namespace Engine {
	Layer::Layer(const std::string& name)
		
	{
		std::cout << name << endl;
		m_DebugName = name;
	}

	Layer::~Layer()
	{

	}
}