#define GLM_ENABLE_EXPERIMENTAL
#include "SceneRaycast.h"
#include "Scene.h"
#include "Panels/MeshFilePath.h"
#include <glm/gtx/intersect.hpp>
#include <cmath>

std::optional<SceneRaycast::Hit> SceneRaycast::Cast(const Scene& scene, const glm::vec3& origin,
	const glm::vec3& direction, float maxDistance, Options options)
{
	const float directionLength = glm::length(direction);
	if (!(maxDistance > 0.0f) || !(directionLength > 0.0f) || !std::isfinite(directionLength))
		return std::nullopt;

	const glm::vec3 worldDirection = direction / directionLength;
	std::optional<Hit> hit;
	for (auto [id, transform, renderer] : scene.m_Registry.view<const Transform, const MeshRender>().each())
	{
		if (renderer.ModelPath.empty() || (options.renderableMeshesOnly && renderer.materials.empty()) ||
			transform.Scale.x == 0.0f || transform.Scale.y == 0.0f || transform.Scale.z == 0.0f)
			continue;
		const auto model = My_map::GetModel(renderer.ModelPath);
		if (!model)
			continue;

		const glm::mat4 inverseModel = glm::inverse(transform.GetTransform());
		const glm::vec3 localOrigin = inverseModel * glm::vec4(origin, 1.0f);
		// Keeping this direction unnormalized preserves world-space hit distances.
		const glm::vec3 localDirection = inverseModel * glm::vec4(worldDirection, 0.0f);
		const bool mirrored = (transform.Scale.x < 0.0f) ^ (transform.Scale.y < 0.0f) ^
			(transform.Scale.z < 0.0f);
		for (const auto& mesh : model->meshes)
		{
			if (options.renderableMeshesOnly && !mesh.IsGPUReady())
				continue;
			for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
			{
				const glm::vec3& a = mesh.vertices[mesh.indices[i]].Position;
				const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].Position;
				const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].Position;
				if (options.cullBackFaces)
				{
					const float facing = glm::dot(glm::cross(b - a, c - a), localDirection);
					if (mirrored ? facing <= 0.0f : facing >= 0.0f)
						continue;
				}

				glm::vec2 barycentric;
				float distance;
				if (glm::intersectRayTriangle(localOrigin, localDirection, a, b, c, barycentric, distance) &&
					distance >= 0.0f && distance <= maxDistance && (!hit || distance < hit->distance))
					hit = Hit{ id, origin + worldDirection * distance, distance };
			}
		}
	}
	return hit;
}
