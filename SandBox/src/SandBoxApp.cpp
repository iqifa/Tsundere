#include<Tsunder.h>
class SandBox:public Engine::Application
{
public:
	SandBox(){}
	~SandBox(){}

private:

};
Engine::Application* Engine::CreateApplication()
{
	std::cout << "Create Sandbox" << endl;
	return new SandBox();
}