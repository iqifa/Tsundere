#pragma once

#include"entt/include/entt.hpp"
#include"Entity.h"
class Entity;
class Scene {
private:
	//unordered_map<UUID, entity>m_EntityMap;

public:
	entt::registry m_Registry;
	template<typename T>
	void OnaddComponent(Entity& entity, T& Component) {}
	void DestoryEntity(Entity entity)
	{
		//m_EntityMap.erase(entity.GetUUID());
		m_Registry.destroy(entity);
	}
	Entity CreateEntity(const string& name)
	{
		return CreateEntityWithUID(UUID(), name);
	}
	Entity CreateEntityWithUID(UUID uuid, const string& name)
	{
		Entity entity = { this,m_Registry.create() };
		entity.AddComponent<ID>(uuid);
		entity.AddComponent<Transform>();
		auto& tag = entity.AddComponent<Tag>();
		tag.tag = name.empty() ? "Entity" : name;

		//m_EntityMap[uuid] = entity;

		return entity;
	}
	void Render()
	{
		
	}
	friend class Entity;

};