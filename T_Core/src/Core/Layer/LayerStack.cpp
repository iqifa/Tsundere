#include "LayerStack.h"
namespace Engine {
	LayerStack::LayerStack()
	{
	}

	LayerStack::~LayerStack()
	{
		for (Layer* layer : Layers)
			delete layer;
	}
	void LayerStack::PushLayer(Layer* layer)
	{
		Layers.push_back(layer);
	}
	void LayerStack::PopLayer(Layer* layer)
	{
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