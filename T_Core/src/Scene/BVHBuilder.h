#pragma once

#include "BVH.h"
#include "Scene/Scene.h"
#include "Scene/Mesh.h"
#include "Scene/Modle.h"
#include "Panels/MeshFilePath.h"
#include "Platform/RHI/RHIBuffer.h"
#include<atomic>

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

	// Global dirty marker: call from anywhere a scene change happens.
	// Next frame's ShadowPass/PathTracePass will rebuild the BVH.
	static void MarkActiveDirty()
	{
		if (s_ActiveInstance)
			s_ActiveInstance->MarkDirty();
	}
	static void SetActiveInstance(BVHBuilder* b) { s_ActiveInstance = b; }

	void GatherTriangles(Ref<Scene> scene);
	void BuildBVH(unsigned int leafSize = 4);

	// GPU upload only (called on main thread after async CPU build completes)
	void UploadToGPU();

	Ref<RHIBuffer> GetTriangleBuffer() const { return m_TriSSBO; }
	Ref<RHIBuffer> GetBVHNodeBuffer() const { return m_BVHSSBO; }

	unsigned int GetTriangleCount() const { return (unsigned int)m_GPUTriangles.size(); }
	unsigned int GetBVHNodeCount() const { return (unsigned int)m_BVHNodes.size(); }

	// Dirty flag: set when scene geometry changes, cleared when BVH is rebuilt.
	// One-frame stale BVH is acceptable; avoids O(N log N) rebuild every frame.
	bool IsDirty() const { return m_Dirty.load(std::memory_order_acquire); }
	void MarkDirty() { m_Dirty.store(true, std::memory_order_release); }
	void MarkClean() { m_Dirty.store(false, std::memory_order_release); }

	// Material-index mapping: provided by PathTracePass before GatherTriangles.
	// Maps CPU Material* → global GPUMaterial index for the Material SSBO.
	void SetMaterialMap(const std::unordered_map<Material*, unsigned int>* map) { m_MaterialMap = map; }

	// For worker thread: set CPU build results, then main thread calls UploadToGPU
	void SetBuildResults(std::vector<GPUTriangle>&& tris, std::vector<GPUBVHNode>&& nodes)
	{
		m_GPUTriangles = std::move(tris);
		m_BVHNodes = std::move(nodes);
	}

private:
	unsigned int BuildNodeRecursive(unsigned int start, unsigned int end, unsigned int depth);

	std::vector<BVHBuildTriangle> m_BuildTriangles;
	std::vector<GPUTriangle> m_GPUTriangles;
	std::vector<GPUBVHNode> m_BVHNodes;

	Ref<RHIBuffer> m_TriSSBO;
	Ref<RHIBuffer> m_BVHSSBO;

	std::atomic<bool> m_Dirty{true};  // starts dirty so first frame builds
	const std::unordered_map<Material*, unsigned int>* m_MaterialMap = nullptr;

	static BVHBuilder* s_ActiveInstance;
};
