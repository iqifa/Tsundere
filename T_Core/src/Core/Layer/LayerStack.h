#pragma once
#include"../Core.h"
#include"HeadLine.h"
#include"Layer.h"
#include<deque>
#include<mutex>
#include<vector>
namespace Engine {
	class T_API LayerStack
	{
	public:
		LayerStack();
		~LayerStack();

		void PushLayer(Layer* layer);
		void PopLayer(Layer* layer);

		// Thread-safe snapshot for iteration.
		// Copy is cheap (pointers only, typically 5-10 layers).
		// Lock is released before returning — callbacks can safely Push/Pop.
		std::vector<Layer*> GetLayerSnapshot() const
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			return std::vector<Layer*>(Layers.begin(), Layers.end());
		}

	private:
		std::deque<Layer*> Layers;
		mutable std::mutex m_Mutex;
	};
}
