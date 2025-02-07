#pragma once

#include"entt/entt.hpp"
#include"Entity.h"
class Entity;
class Scene {
private:
	//unordered_map<UID, entity>m_EntityMap;

public:
	entt::registry m_Registry;
	template<typename T>
	void OnaddComponent(Entity& entity, T& Component) {}
	void DestoryEntity(Entity entity)
	{
		entity.setparenwithdelself(null);

		DestoryChild(entity);
		m_Registry.destroy(entity);
	}
	void DestoryChild(Entity entity)
	{
		auto& child = entity.GetComponent<Child>();
		for (auto childID : child.children)
		{
			Entity child{ this,childID };
			DestoryChild(child);
			m_Registry.destroy(child);
		}
	}
	Entity CreateEntity(const std::string& name)
	{
		return CreateEntityWithUID(UID(), name);
	}
	Entity CreateEntityWithUID(UID UID, const std::string& name)
	{
		Entity entity = { this,m_Registry.create() };
		entity.AddComponent<ID>(UID);
		entity.AddComponent<Top>();
		entity.AddComponent<Child>();
		entity.AddComponent<Parent>();
		entity.AddComponent<Transform>();
		auto& tag = entity.AddComponent<Tag>();
		tag.tag = name.empty() ? "Entity" : name;

		//m_EntityMap[UID] = entity; 

		return entity;
	}
	void Render()
	{
		
	}
	friend class Entity;

};