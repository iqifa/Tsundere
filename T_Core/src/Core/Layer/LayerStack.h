#pragma once
#include"../Core.h"
#include"HeadLine.h"
#include"Layer.h"
#include<deque>
namespace Engine {
	class T_API LayerStack
	{
	public:
		LayerStack();
		~LayerStack();

		void PushLayer(Layer* layer);
		void PopLayer(Layer* layer);
		
		std::deque<Layer*>::iterator begin() { return Layers.begin(); }
		std::deque<Layer*>::iterator end() { return Layers.end(); }

	private:
		std::deque<Layer*> Layers;
	};
}
