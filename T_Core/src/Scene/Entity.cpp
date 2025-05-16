#include"Entity.h"

void Entity::Destroy()
{
	for (auto childid : GetComponent<Child>().children)
	{
		Entity{ m_Scene,childid }.Destroy();
	}
}