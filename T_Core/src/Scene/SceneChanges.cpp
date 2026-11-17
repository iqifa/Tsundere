#include "SceneChanges.h"

#include "Scene/Component.h"
#include "Scene/Mesh.h"

#include <atomic>

namespace
{
	// 主线程之外（资源加载完成回调等）也可能通知，用原子计数。
	std::atomic<uint64_t> g_Versions[SceneChanges::CategoryCount];
	std::atomic<uint64_t> g_NextGeometryId{ 1 };

	void OnMeshRenderChanged(entt::registry&, entt::entity)
	{
		SceneChanges::NotifyMask(SceneChanges::Bit(SceneChanges::Category::InstanceSet) |
			SceneChanges::Bit(SceneChanges::Category::MaterialAssignment));
	}

	void OnLightChanged(entt::registry&, entt::entity)
	{
		SceneChanges::Notify(SceneChanges::Category::Lights);
	}

	void OnTransformPatched(entt::registry& registry, entt::entity entity)
	{
		SceneChanges::NotifyTransform(registry, entity);
	}
}

namespace SceneChanges
{
	const char* CategoryName(Category c)
	{
		switch (c)
		{
		case Category::MeshGeometry:       return "MeshGeometry";
		case Category::InstanceTransform:  return "InstanceTransform";
		case Category::InstanceSet:        return "InstanceSet";
		case Category::MaterialAssignment: return "MaterialAssignment";
		case Category::MaterialParameters: return "MaterialParameters";
		case Category::Lights:             return "Lights";
		default:                           return "?";
		}
	}

	std::string DescribeMask(uint32_t mask)
	{
		std::string out;
		for (size_t i = 0; i < CategoryCount; ++i)
		{
			if (!(mask & (1u << i)))
				continue;
			if (!out.empty())
				out += '|';
			out += CategoryName(static_cast<Category>(i));
		}
		return out.empty() ? "none" : out;
	}

	void Notify(Category c)
	{
		g_Versions[static_cast<size_t>(c)].fetch_add(1, std::memory_order_acq_rel);
	}

	void NotifyMask(uint32_t mask)
	{
		for (size_t i = 0; i < CategoryCount; ++i)
			if (mask & (1u << i))
				Notify(static_cast<Category>(i));
	}

	uint64_t Version(Category c)
	{
		return g_Versions[static_cast<size_t>(c)].load(std::memory_order_acquire);
	}

	Versions Snapshot()
	{
		Versions v;
		for (size_t i = 0; i < CategoryCount; ++i)
			v.value[i] = g_Versions[i].load(std::memory_order_acquire);
		return v;
	}

	void NotifyTransform(const entt::registry& registry, entt::entity entity)
	{
		if (!registry.valid(entity))
			return;
		if (registry.all_of<Component::MeshRender>(entity))
			Notify(Category::InstanceTransform);
		if (registry.all_of<Component::Light>(entity))
			Notify(Category::Lights);
	}

	void NotifyActivation()
	{
		NotifyMask(Bit(Category::InstanceSet) | Bit(Category::Lights));
	}

	void NotifyModelChanged()
	{
		NotifyMask(Bit(Category::InstanceSet) | Bit(Category::MaterialAssignment));
	}

	void NotifyModelBinding()
	{
		NotifyMask(Bit(Category::InstanceSet) | Bit(Category::MaterialAssignment));
	}

	void NotifyMaterialAssignment()
	{
		Notify(Category::MaterialAssignment);
	}

	void NotifyMeshGeometry(Mesh& mesh)
	{
		++mesh.GeometryRevision;
		Notify(Category::MeshGeometry);
	}

	void Attach(entt::registry& registry)
	{
		registry.on_construct<Component::MeshRender>().connect<&OnMeshRenderChanged>();
		registry.on_destroy<Component::MeshRender>().connect<&OnMeshRenderChanged>();
		registry.on_update<Component::MeshRender>().connect<&OnMeshRenderChanged>();
		registry.on_construct<Component::Light>().connect<&OnLightChanged>();
		registry.on_destroy<Component::Light>().connect<&OnLightChanged>();
		registry.on_update<Component::Light>().connect<&OnLightChanged>();
		registry.on_update<Component::Transform>().connect<&OnTransformPatched>();
	}

	uint64_t NewGeometryId()
	{
		return g_NextGeometryId.fetch_add(1, std::memory_order_relaxed);
	}
}
