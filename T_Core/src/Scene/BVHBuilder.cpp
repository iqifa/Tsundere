#include "BVHBuilder.h"

BVHBuilder* BVHBuilder::s_ActiveInstance = nullptr;

void BVHBuilder::GatherTriangles(Ref<Scene> scene)
{
	m_BuildTriangles.clear();
	m_GPUTriangles.clear();
	m_BVHNodes.clear();

	for (auto [entityID, transform, meshrender] :
		scene->m_Registry.view<Component::Transform, Component::MeshRender>().each())
	{
		if (meshrender.ModelPath.empty())
			continue;

		Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
		if (!model || model->meshes.empty())
			continue;

		glm::mat4 worldMat = transform.GetTransform();
		glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(worldMat)));

		for (size_t meshIdx = 0; meshIdx < model->meshes.size(); meshIdx++)
		{
			auto& mesh = model->meshes[meshIdx];

			// Resolve material index: use global mapping if available (PathTracePass),
			// otherwise fall back to mesh-local index (ShadowPass doesn't use it).
			unsigned int matIdx = 0;
			if (m_MaterialMap && meshIdx < meshrender.materials.size())
			{
				auto it = m_MaterialMap->find(meshrender.materials[meshIdx].get());
				if (it != m_MaterialMap->end())
					matIdx = it->second;
			}
			else
			{
				matIdx = (unsigned int)(meshIdx < meshrender.materials.size()
					? meshIdx : 0);
			}

			for (size_t i = 0; i < mesh.indices.size(); i += 3)
			{
				BVHBuildTriangle bt;

				for (int j = 0; j < 3; j++)
				{
					unsigned int idx = mesh.indices[i + j];
					auto& vert = mesh.vertices[idx];

					glm::vec4 wp = worldMat * glm::vec4(vert.Position, 1.0f);
					glm::vec3 wn = glm::normalize(normalMat * vert.Normal);

					if (j == 0)
					{
						bt.v0 = glm::vec3(wp);
						bt.n0 = wn;
						bt.uv0 = vert.TexCoords;
					}
					else if (j == 1)
					{
						bt.v1 = glm::vec3(wp);
						bt.n1 = wn;
						bt.uv1 = vert.TexCoords;
					}
					else
					{
						bt.v2 = glm::vec3(wp);
						bt.n2 = wn;
						bt.uv2 = vert.TexCoords;
					}
				}

				bt.centroid = (bt.v0 + bt.v1 + bt.v2) / 3.0f;
				bt.materialIndex = matIdx;
				m_BuildTriangles.push_back(bt);
			}
		}
	}
}

void BVHBuilder::BuildBVH(unsigned int leafSize)
{
	m_GPUTriangles.clear();
	m_BVHNodes.clear();

	if (m_BuildTriangles.empty())
	{
		GPUBVHNode root;
		root.bboxMin = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
		root.bboxMax = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
		m_BVHNodes.push_back(root);

		GPUTriangle dummy;
		memset(&dummy, 0, sizeof(dummy));
		m_GPUTriangles.push_back(dummy);
	}
	else
	{
		BuildNodeRecursive(0, (unsigned int)m_BuildTriangles.size(), 0);
	}
	// UploadToGPU() must be called separately (on main/GL thread after this)
}

void BVHBuilder::UploadToGPU()
{
	m_TriSSBO = RHIBuffer::Create(BufferDesc{
		(uint32_t)(m_GPUTriangles.size() * sizeof(GPUTriangle)),
		BufferUsage::Storage, false, m_GPUTriangles.data() });

	m_BVHSSBO = RHIBuffer::Create(BufferDesc{
		(uint32_t)(m_BVHNodes.size() * sizeof(GPUBVHNode)),
		BufferUsage::Storage, false, m_BVHNodes.data() });
}

unsigned int BVHBuilder::BuildNodeRecursive(unsigned int start, unsigned int end, unsigned int depth)
{
	// Compute AABB of this range
	glm::vec3 aabbMin(FLT_MAX);
	glm::vec3 aabbMax(-FLT_MAX);

	for (unsigned int i = start; i < end; i++)
	{
		auto& tri = m_BuildTriangles[i];
		aabbMin = glm::min(aabbMin, tri.v0); aabbMax = glm::max(aabbMax, tri.v0);
		aabbMin = glm::min(aabbMin, tri.v1); aabbMax = glm::max(aabbMax, tri.v1);
		aabbMin = glm::min(aabbMin, tri.v2); aabbMax = glm::max(aabbMax, tri.v2);
	}

	unsigned int triCount = end - start;

	// Create leaf if few triangles or max depth
	if (triCount <= 4 || depth >= 24)
	{
		GPUBVHNode node;
		node.bboxMin = glm::vec4(aabbMin, -((float)(int)m_GPUTriangles.size() + 1.0f)); // negative = leaf, encode start index
		node.bboxMax = glm::vec4(aabbMax, (float)triCount);

		m_BVHNodes.push_back(node);

		// Append triangles to GPU array
		for (unsigned int i = start; i < end; i++)
		{
			auto& bt = m_BuildTriangles[i];
			GPUTriangle gpu;
			gpu.v0 = glm::vec4(bt.v0, 0.0f);
			gpu.v1 = glm::vec4(bt.v1, 0.0f);
			gpu.v2 = glm::vec4(bt.v2, 0.0f);
			gpu.n0 = glm::vec4(bt.n0, (float)bt.materialIndex);
			gpu.n1 = glm::vec4(bt.n1, 0.0f);
			gpu.n2 = glm::vec4(bt.n2, 0.0f);
			gpu.uv0 = glm::vec4(bt.uv0, 0.0f, 0.0f);
			gpu.uv1 = glm::vec4(bt.uv1, 0.0f, 0.0f);
			gpu.uv2 = glm::vec4(bt.uv2, 0.0f, 0.0f);
			m_GPUTriangles.push_back(gpu);
		}

		return (unsigned int)m_BVHNodes.size() - 1;
	}

	// Find best split axis (longest extent)
	glm::vec3 extent = aabbMax - aabbMin;
	int bestAxis = 0;
	if (extent.y > extent.x) bestAxis = 1;
	if (extent.z > extent[bestAxis]) bestAxis = 2;

	// Sort triangles along best axis by centroid
	std::sort(m_BuildTriangles.begin() + start, m_BuildTriangles.begin() + end,
		[bestAxis](const BVHBuildTriangle& a, const BVHBuildTriangle& b) {
			return a.centroid[bestAxis] < b.centroid[bestAxis];
		});

	// Split at midpoint
	unsigned int mid = (start + end) / 2;

	// Create placeholder for this internal node, build children
	unsigned int nodeIdx = (unsigned int)m_BVHNodes.size();
	m_BVHNodes.push_back(GPUBVHNode()); // placeholder

	unsigned int leftChild = BuildNodeRecursive(start, mid, depth + 1);
	unsigned int rightChild = BuildNodeRecursive(mid, end, depth + 1);

	// Fill in this node
	m_BVHNodes[nodeIdx].bboxMin = glm::vec4(aabbMin, (float)leftChild);
	m_BVHNodes[nodeIdx].bboxMax = glm::vec4(aabbMax, (float)rightChild);

	return nodeIdx;
}
