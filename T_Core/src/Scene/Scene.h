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
	void DestoryEntity(Entity Destory)
	{
		Destory.setparenwithdelself(null);

		DestoryChild(Destory);
		Info_Core("Delete {}", Destory.GetComponent<Tag>().tag);
		m_Registry.destroy(Destory);
	}
	void DestoryChild(Entity parent)
	{
		auto& child_componrnt = parent.GetComponent<Child>();
		for (auto childID : child_componrnt.children)
		{
			Entity child_entity{ this,childID };
			DestoryChild(child_entity);
			Info_Core("Delete {}", child_entity.GetComponent<Tag>().tag);
			m_Registry.destroy(child_entity);
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