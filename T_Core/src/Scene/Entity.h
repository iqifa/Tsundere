#pragma once
#include"entt/entt.hpp"
#include"Scene/Component.h"
#include"Panels/Material.h"
using namespace Component;
using namespace entt;
using namespace glm;

class Scene;

class  Entity
{
	friend class Scene;
private:
	Scene* m_Scene = nullptr;
	entity m_EntityHandle{ null };
public:
	Entity() = default;
	Entity(Scene* sence, entity handle) :m_Scene(sence), m_EntityHandle(handle) {}
	Entity(const Entity& other) = default;
	~Entity() = default;

	void addchild(entt::entity childID)
	{
		GetComponent<Child>().addChild(childID);
	}
	void delchild(entt::entity childID)
	{
		GetComponent<Child>().removeChild(childID);
	}
	void changeparent(entt::entity parentID)
	{	
		GetComponent<Parent>().ChangeParent(parentID);
	}
	
	void addchildwithchangeparent(entt::entity childID)
	{
		addchild(childID);
		//auto& prepar = m_Scene->m_Registry.get<Parent>(childID);
		auto& prepar = Entity{ m_Scene,childID }.GetComponent<Parent>();
		if (prepar.parent == null)
			Entity{ m_Scene,childID }.RemoveComponent<Top>();
		else {
			//auto& children= m_Scene->m_Registry.get<Child>(prepar.parent);
			auto& children = Entity{ m_Scene,prepar.parent }.GetComponent<Child>();
			children.removeChild(childID);
		}
		prepar.ChangeParent(m_EntityHandle);
	}
	void setparenwithdelself(entt::entity parentID)
	{
		auto& prepar = GetComponent<Parent>();

		if (parentID == prepar.parent)
			return;

		if (prepar.parent == null)
		{
			RemoveComponent<Top>();
		}
		else {
			Entity{ m_Scene,prepar.parent }.GetComponent<Child>().removeChild(m_EntityHandle);
			if (parentID == null)
			{
				AddComponent<Top>();
			}
			else {
				Entity{ m_Scene,parentID }.GetComponent<Child>().addChild(m_EntityHandle);
			}
		}
		prepar.ChangeParent(parentID);
	}

	template<typename T, typename...Args>
	T& AddComponent(Args&&...args)
	{
		if (HasComponent<T>())
		{

			//TODO:
			debugerror("Component Has Exist!!!");
			T& component = m_Scene->m_Registry.get<T>(m_EntityHandle);
			return component;
		}
		T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
		m_Scene->OnaddComponent(*this, component);
		return component;
	}
	template<typename T>
	T& GetComponent()
	{
		return m_Scene->m_Registry.get<T>(m_EntityHandle);
	}
	template<typename T>
	bool HasComponent()
	{
		return m_Scene->m_Registry.any_of<T>(m_EntityHandle);
	}

	template<typename T>
	void RemoveComponent()
	{
		if (HasComponent<T>())
			m_Scene->m_Registry.remove<T>(m_EntityHandle);
	}


	UID& GetUID() { return GetComponent<ID>().id; }
	bool operator== (const Entity& other)const { return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; }
	bool operator!=(const Entity& other)const { return !(*this == other); }
	operator bool() { return m_EntityHandle != entt::null; }
	operator entity() { return m_EntityHandle; }
	operator uint32_t()const { return (uint32_t)m_EntityHandle; }
};

