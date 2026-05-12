#pragma once

#include "BVH.h"
#include "Scene/Scene.h"
#include "Scene/Mesh.h"
#include "Scene/Modle.h"
#include "Panels/MeshFilePath.h"
#include "Platform/GL/StorageBuffer.h"

struct BVHBuildTriangle
{
	glm::vec3 v0, v1, v2;
	glm::vec3 n0, n1, n2;
	glm::vec2 uv0, uv1, uv2;
	glm::vec3 centroid;
	unsigned int materialIndex;
};

class T_API BVHBuilder
{
public:
	BVHBuilder() = default;

	void GatherTriangles(Ref<Scene> scene);
	void BuildBVH(unsigned int leafSize = 4);

	Ref<StorageBuffer> GetTriangleBuffer() const { return m_TriSSBO; }
	Ref<StorageBuffer> GetBVHNodeBuffer() const { return m_BVHSSBO; }

	unsigned int GetTriangleCount() const { return (unsigned int)m_GPUTriangles.size(); }
	unsigned int GetBVHNodeCount() const { return (unsigned int)m_BVHNodes.size(); }

private:
	unsigned int BuildNodeRecursive(unsigned int start, unsigned int end, unsigned int depth);

	std::vector<BVHBuildTriangle> m_BuildTriangles;
	std::vector<GPUTriangle> m_GPUTriangles;
	std::vector<GPUBVHNode> m_BVHNodes;

	Ref<StorageBuffer> m_TriSSBO;
	Ref<StorageBuffer> m_BVHSSBO;
};
