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
		//m_EntityMap.erase(entity.GetUID());
		m_Registry.destroy(entity);
	}
	Entity& CreateEntity(const std::string& name)
	{
		return CreateEntityWithUID(UID(), name);
	}
	Entity& CreateEntityWithUID(UID UID, const std::string& name)
	{
		Entity entity = { this,m_Registry.create() };
		entity.AddComponent<ID>(UID);
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