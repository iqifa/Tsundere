#pragma once

#include "Core/Core.h"
#include <entt/entt.hpp>
#include <glm/vec3.hpp>
#include <optional>

class Scene;

namespace SceneRaycast
{
	struct Hit
	{
		entt::entity entity;
		glm::vec3 position;
		float distance;
	};

	struct Options
	{
		bool cullBackFaces = false;
		bool renderableMeshesOnly = false;
	};

	// Tests scene mesh triangles. Origin, direction, and distances are in world space.
	// Direction need not be normalized. Models still loading cannot be hit.
	T_API std::optional<Hit> Cast(const Scene& scene, const glm::vec3& origin,
		const glm::vec3& direction, float maxDistance, Options options = {});
}
