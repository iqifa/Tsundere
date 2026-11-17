#pragma once

#include"entt/entt.hpp"
#include"Entity.h"
#include"Debug/Debug.h"
#include"Scene/SceneChanges.h"
class Entity;
class Scene {
private:
	//unordered_map<UID, entity>m_EntityMap;

public:
	entt::registry m_Registry;
	uint64_t Revision = 0;

	// MeshRender / Light 的增删和 patch 自动发出 SceneChanges 通知。
	// SceneSerializer 用 registry.swap 换入新场景：两边都挂了信号，换完照样有效。
	Scene() { SceneChanges::Attach(m_Registry); }

	bool IsActive(entt::entity entity) const
	{
		// Bound the walk so malformed parent cycles cannot hang rendering.
		size_t remaining = m_Registry.storage<entt::entity>()->size();
		while (entity != entt::null && remaining-- > 0)
		{
			if (!m_Registry.valid(entity)) return false;
			const auto* tag = m_Registry.try_get<Component::Tag>(entity);
			if (tag && !tag->Active) return false;
			const auto* parent = m_Registry.try_get<Component::Parent>(entity);
			entity = parent ? parent->parent : entt::null;
		}
		return entity == entt::null;
	}
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
