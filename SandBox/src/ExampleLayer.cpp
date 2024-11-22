#include "ExampleLayer.h"
#include <Debug/Debug.h>



ExampleLayer::ExampleLayer(string name):Layer(name)
{
	
}

void ExampleLayer::OnUpdate()
{
	
}

void ExampleLayer::OnEvent(Eventing::Event<>& event)
{
	event.Invoke();
}