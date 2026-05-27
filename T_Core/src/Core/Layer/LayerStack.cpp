#include "LayerStack.h"
using namespace std;
namespace Engine {
	LayerStack::LayerStack()
	{
	}

	LayerStack::~LayerStack()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (Layer* layer : Layers)
			delete layer;
	}
	void LayerStack::PushLayer(Layer* layer)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		Layers.push_back(layer);
	}
	void LayerStack::PopLayer(Layer* layer)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		deque<Layer*>temp;

		while (!Layers.empty())
		{
			if (Layers.front() != layer)
			{
				temp.push_back(Layers.front());
				Layers.pop_front();
			}
			else {
				Layers.pop_front();
				break;
			}
		}

		while (!temp.empty()) {
			Layers.push_front(temp.back());
			temp.pop_back();
		}
	}
}
