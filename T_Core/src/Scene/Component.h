#pragma once
#ifndef COMPONENT
#define COMPONENT

#include"Trans/SceneCamera.h"
#include"GLHead.h"
#include"CubeMap.h"
#include"Panels/Material.h"
#include<random>
#include "Modle.h"
#include <entt/entt.hpp>

static std::random_device s_RandomDevice;
static std::mt19937_64 s_Engine(s_RandomDevice());
static std::uniform_int_distribution<uint64_t> s_UniformDistribution;

class Entity;
class UID {
public:
	UID() : m_UID(s_UniformDistribution(s_Engine)) {}
	UID(uint64_t UID) :m_UID(UID) {}
	UID(const UID&) = default;

	operator uint64_t() { return m_UID; }

private:
	uint64_t m_UID;
};
namespace Component
{
	struct  ID
	{
	public :
		ID() = default;
		ID(const ID&) = default;

		UID id;
	};

	struct Transform
	{
		vec3 Position = { 0.0f,0.0f,0.0f };
		vec3 Rotation = { 0.0f,0.0f,0.0f };
		vec3 Scale = { 1.0f,1.0f,1.0f };

		Transform() = default;
		Transform(const Transform&) = default;
		Transform(const vec3& Position) :Position(Position) {}

		mat4 GetTransform() const
		{
			mat4 rotation = translate(glm::identity<mat4>(), glm::vec3(0.0f, 0.0f, 0.0f));
			rotation = rotate(rotation, Rotation.x, vec3(1.0f, 0.0f, 0.0f));
			rotation = rotate(rotation, Rotation.y, vec3(0.0f, 1.0f, 0.0f));
			rotation = rotate(rotation, Rotation.z, vec3(0.0f, 0.0f, 1.0f));
			return translate(mat4(1.0f), Position) * rotation * scale(mat4(1.0f), Scale);
		}

		mat4 GetMVP()const
		{
			mat4 view = currentcamera->GetViewFront();
			mat4 proj = currentcamera->GetProj();
			return proj * view * GetTransform();
		}
	};

	struct Top {
		std::string tag;

		Top() = default;
		Top(const Top&) = default;
		Top(const std::string& tag) :tag(tag) {}
	};
	struct Tag {
		std::string tag;

		Tag() = default;
		Tag(const Tag&) = default;
		Tag(const std::string& tag) :tag(tag) {}
	};

	struct Child {
		std::set<entt::entity>children;
		Child() = default;
		Child(const Child&) = default;
		Child(std::set<entt::entity>handles) { children = handles; }
		void addChild(entt::entity child)
		{
			children.insert(child);
		}
		void removeChild(entt::entity child)
		{
			children.erase(child);
		}
	};
	struct Parent {
		entt::entity parent;
		Parent() = default;
		Parent(const Parent&) = default;
		Parent(entt::entity parent) { this->parent = parent; };

		void ChangeParent(entt::entity parent)
		{
			this->parent = parent;
		}
	};
	struct Camera
	{
		vec3 Target;
	};

	struct MeshRender {
		std::vector<Ref<Material>> materials;
	};
}

#endif // !COMPONENT

