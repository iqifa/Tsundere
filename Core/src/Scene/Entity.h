#pragma once
#include"entt/include/entt.hpp"
#include"Scene/Component.h"
#include"Panels/Material.h"
using namespace Component;
using namespace entt;

class Scene;

class  Entity
{
private:
	Scene* m_Scene = nullptr;
	entity m_EntityHandle{ null };
public:
	Entity() = default;
	Entity(Scene* sence, entity handle) :m_Scene(sence), m_EntityHandle(handle) {}
	Entity(const Entity& other) = default;
	~Entity() = default;

	template<typename T, typename...Args>
	T& AddComponent(Args&&...args)
	{
		T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
		m_Scene->OnaddComponent(*this, component);
		return component;
	}
	//template<typename...Args>
	//MeshComponent& AddComponent<MeshComponent>(Args&&...args)
	//{
	//	//if (HasComponent<Material>())
	//	//{
	//	//	MeshComponent& component= m_Scene->m_Registry.emplace<MeshComponent>(m_EntityHandle, std::forward<Args>(args)...);
	//	//	return component;
	//	//}
	///*	Shader shader("res/shaders/default.shader");
	//	AddComponent<Material>(shader);*/
	//	MeshComponent& component = m_Scene->m_Registry.emplace<MeshComponent>(m_EntityHandle, std::forward<Args>(args)...);
	//	return component;
	//}

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
		m_Scene->m_Registry.remove<T>(m_EntityHandle);
	}

	void Draw()
	{
			auto& component = GetComponent<MeshFile>();
			if (component.m_ModleFile != -1)
			{
				Material material = GetComponent<Material>();
				Ref<Shader> shader =material.shader;
				shader->Bind();
				material.Render(shader);
				mat4 mvp = GetComponent<Transform>().GetMVP();
				mat4 model = GetComponent<Transform>().GetTransform();
				shader->SetUniformMat4f("u_MVP", mvp);
				shader->SetUniformMat4f("model", model);
				//shader->SetUniform1i("texturesize", 6);
				component.m_modle->Draw(*shader);
			}
	}


	UID& GetUID() { return GetComponent<ID>().id; }
	bool operator== (const Entity& other)const { return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; }
	bool operator!=(const Entity& other)const { return !(*this == other); }
	operator bool() { return m_EntityHandle != entt::null; }
	operator entity() { return m_EntityHandle; }
	operator uint32_t()const { return (uint32_t)m_EntityHandle; }
};

