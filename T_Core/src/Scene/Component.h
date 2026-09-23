#pragma once
#ifndef COMPONENT
#define COMPONENT

#include"Trans/SceneCamera.h"
#include"GLHead.h"
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
		entt::entity parent = entt::null;
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
		std::string ModelPath;
		std::vector<Ref<Material>> materials;
	};

	struct DirectionalLight
	{
		vec3 Direction = { -0.5f, -1.0f, -0.5f };
		vec3 Color = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		float Ambient = 0.1f;
	};

	struct PointLight
	{
		vec3 Color = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
		float Radius = 5.0f;
		float Falloff = 1.0f;
	};

	struct Light {
		enum class LightType : uint8_t {
			Directional = 0,
			Point = 1,
			Spot = 2,
			Area = 3
		};

		LightType type = LightType::Directional;
		uint8_t pad[3] = { 0, 0, 0 }; // Padding to align the struct to 16 bytes

		vec3 Color = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;

		vec3 Direction = { -0.5f, -1.0f, -0.5f };
		float Ambient = 0.1f;


		union {
			struct {
				float Radius = 5.0f;
				float Falloff = 1.0f;
				float _unpad[2] = { 0.0f, 0.0f }; // Padding to align the struct to 16 bytes
			} Attenuation;;

			struct {
				float Radius = 5.0f;
				float Falloff = 1.0f;
				float InnerAngle = 15.0f;
				float OuterAngle = 30.0f;
			}Spot;
			struct {
				float Width = 1.0f;
				float Height = 1.0f;
				uint32_t Shape = 0; // 0: Rectangle, 1: Disk
				float _unpad;
			}Area;

			float Data[4];
		}Params;
	};
}

#endif // !COMPONENT

