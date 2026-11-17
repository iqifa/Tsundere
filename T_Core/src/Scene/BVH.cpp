#include "BVH.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
	constexpr uint32_t kMaxBins = 32;

	bool IsFinite(const glm::vec3& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	// -----------------------------------------------------------------------
	// 分箱 SAH 构建器
	//
	// 深度保证：进入节点时始终满足 count <= maxLeaf * 2^(剩余深度)。
	// SAH 切分可能很不均衡，所以只有当两个孩子无论怎么分都还满足该不变式时
	// 才用 SAH，否则改用对半的中位切分。这样叶子大小和树深都有硬上限，
	// 着色器可以放心用定长栈。
	// -----------------------------------------------------------------------
	struct TreeBuilder
	{
		const BVHAABB*         bounds = nullptr;
		std::vector<glm::vec3> centroids;
		uint32_t               maxLeafSize = 4;
		uint32_t               binCount = 16;
		uint32_t               maxDepth = 32;
		BVHTree*               out = nullptr;

		struct Split
		{
			int      axis = -1;
			uint32_t bin = 0;     // 图元 bin <= split.bin 归左
			float    cost = FLT_MAX;
		};

		uint32_t BinOf(uint32_t prim, int axis, float cmin, float scale) const
		{
			const float f = (centroids[prim][axis] - cmin) * scale;
			const uint32_t b = f > 0.0f ? static_cast<uint32_t>(f) : 0u;
			return (std::min)(b, binCount - 1);
		}

		Split FindBestSplit(uint32_t first, uint32_t count, const BVHAABB& centroidBounds) const
		{
			Split best;
			const glm::vec3 extent = centroidBounds.Extent();
			uint32_t* order = out->primitiveOrder.data();

			for (int axis = 0; axis < 3; ++axis)
			{
				if (!(extent[axis] > 0.0f))
					continue; // 该轴上质心全部重合，无法切分

				const float cmin = centroidBounds.min[axis];
				const float scale = static_cast<float>(binCount) / extent[axis];

				BVHAABB  binBounds[kMaxBins];
				uint32_t binCounts[kMaxBins] = {};
				for (uint32_t i = 0; i < count; ++i)
				{
					const uint32_t prim = order[first + i];
					const uint32_t b = BinOf(prim, axis, cmin, scale);
					binBounds[b].Grow(bounds[prim]);
					++binCounts[b];
				}

				// 右侧前缀：rightArea[i] / rightCount[i] 覆盖 bin (i, binCount)
				float    rightArea[kMaxBins] = {};
				uint32_t rightCount[kMaxBins] = {};
				BVHAABB  acc;
				uint32_t n = 0;
				for (uint32_t i = binCount - 1; i > 0; --i)
				{
					acc.Grow(binBounds[i]);
					n += binCounts[i];
					rightArea[i - 1] = acc.HalfArea();
					rightCount[i - 1] = n;
				}

				acc = BVHAABB();
				n = 0;
				for (uint32_t i = 0; i + 1 < binCount; ++i)
				{
					acc.Grow(binBounds[i]);
					n += binCounts[i];
					if (n == 0 || rightCount[i] == 0)
						continue;
					const float cost = acc.HalfArea() * static_cast<float>(n) +
						rightArea[i] * static_cast<float>(rightCount[i]);
					if (cost < best.cost)
					{
						best.cost = cost;
						best.axis = axis;
						best.bin = i;
					}
				}
			}
			return best;
		}

		void Subdivide(uint32_t nodeIndex, uint32_t first, uint32_t count, uint32_t depth)
		{
			uint32_t* order = out->primitiveOrder.data();

			BVHAABB nodeBounds;
			BVHAABB centroidBounds;
			for (uint32_t i = 0; i < count; ++i)
			{
				const uint32_t prim = order[first + i];
				nodeBounds.Grow(bounds[prim]);
				centroidBounds.Grow(centroids[prim]);
			}

			{
				GPUBVHNode& node = out->nodes[nodeIndex];
				node.aabbMin = nodeBounds.min;
				node.aabbMax = nodeBounds.max;
			}
			out->depth = (std::max)(out->depth, depth);

			const uint32_t remaining = maxDepth - depth;
			if (count <= maxLeafSize || remaining == 0)
			{
				GPUBVHNode& node = out->nodes[nodeIndex];
				node.leftFirst = first;
				node.count = count;
				return;
			}

			// 孩子的剩余深度是 remaining - 1，容量 = maxLeaf * 2^(remaining-1)。
			const uint64_t childCapacity = (remaining - 1 >= 40)
				? UINT64_MAX
				: (static_cast<uint64_t>(maxLeafSize) << (remaining - 1));
			const bool sahAllowed = static_cast<uint64_t>(count) <= childCapacity;

			uint32_t mid = first;
			if (sahAllowed)
			{
				const Split split = FindBestSplit(first, count, centroidBounds);
				if (split.axis >= 0)
				{
					const float cmin = centroidBounds.min[split.axis];
					const float scale = static_cast<float>(binCount) / centroidBounds.Extent()[split.axis];
					uint32_t* begin = order + first;
					uint32_t* end = begin + count;
					uint32_t* middle = std::partition(begin, end, [&](uint32_t prim) {
						return BinOf(prim, split.axis, cmin, scale) <= split.bin;
					});
					mid = first + static_cast<uint32_t>(middle - begin);
				}
			}

			if (mid == first || mid == first + count)
			{
				// 中位切分：最长轴上按质心对半分。质心全部重合时退化为按下标对半分。
				const glm::vec3 extent = centroidBounds.Extent();
				int axis = 0;
				if (extent.y > extent.x) axis = 1;
				if (extent.z > extent[axis]) axis = 2;
				mid = first + count / 2;
				std::nth_element(order + first, order + mid, order + first + count,
					[&](uint32_t a, uint32_t b) { return centroids[a][axis] < centroids[b][axis]; });
			}

			const uint32_t left = static_cast<uint32_t>(out->nodes.size());
			out->nodes.emplace_back();
			out->nodes.emplace_back();
			out->nodes[nodeIndex].leftFirst = left;
			out->nodes[nodeIndex].count = 0;

			Subdivide(left, first, mid - first, depth + 1);
			Subdivide(left + 1, mid, first + count - mid, depth + 1);
		}
	};

	// 占位 BLAS：1 个叶子 + 1 个全零的退化三角形。固定占据 0 号位。
	void AppendPlaceholderBLAS(BVHSceneData& data)
	{
		GPUBVHNode leaf;
		leaf.leftFirst = 0;
		leaf.count = 1;
		data.blasNodes.push_back(leaf);
		data.triangles.emplace_back();
		data.attribs.emplace_back();
	}

	// ----- 遍历 -----

	struct Ray
	{
		glm::vec3 origin;
		glm::vec3 direction;
		glm::vec3 invDirection;
	};

	Ray MakeRay(const glm::vec3& origin, const glm::vec3& direction)
	{
		return Ray{ origin, direction, glm::vec3(1.0f) / direction };
	}

	// slab 测试。min/max 的参数顺序保证 0 * inf 产生的 NaN 被忽略。
	bool IntersectAABB(const Ray& ray, const glm::vec3& bmin, const glm::vec3& bmax, float tMax, float& tEntry)
	{
		float tNear = 0.0f;
		float tFar = tMax;
		for (int axis = 0; axis < 3; ++axis)
		{
			const float t0 = (bmin[axis] - ray.origin[axis]) * ray.invDirection[axis];
			const float t1 = (bmax[axis] - ray.origin[axis]) * ray.invDirection[axis];
			tNear = (std::max)(tNear, (std::min)(t0, t1));
			tFar = (std::min)(tFar, (std::max)(t0, t1));
		}
		tEntry = tNear;
		return tNear <= tFar;
	}

	// Möller–Trumbore，双面。
	bool IntersectTriangle(const Ray& ray, const GPUBVHTriangle& tri, float tMax, float& t, float& u, float& v)
	{
		const glm::vec3 e1 = tri.v1 - tri.v0;
		const glm::vec3 e2 = tri.v2 - tri.v0;
		const glm::vec3 p = glm::cross(ray.direction, e2);
		const float det = glm::dot(e1, p);
		if (!(std::fabs(det) > 1e-20f))
			return false; // 平行 / 退化（含占位三角形）
		const float invDet = 1.0f / det;
		const glm::vec3 s = ray.origin - tri.v0;
		u = glm::dot(s, p) * invDet;
		if (u < 0.0f || u > 1.0f)
			return false;
		const glm::vec3 q = glm::cross(s, e1);
		v = glm::dot(ray.direction, q) * invDet;
		if (v < 0.0f || u + v > 1.0f)
			return false;
		t = glm::dot(e2, q) * invDet;
		return t > 0.0f && t < tMax;
	}

	bool TraverseBLAS(const BVHSceneData& data, const GPUBVHInstance& instance, uint32_t instanceIndex,
		const Ray& ray, BVHHit& hit, bool anyHit)
	{
		const GPUBVHNode* nodes = data.blasNodes.data() + instance.blasNodeOffset;
		const GPUBVHTriangle* triangles = data.triangles.data() + instance.triangleOffset;

		uint32_t stack[kBVHMaxSupportedDepth + 1];
		uint32_t stackSize = 0;
		uint32_t nodeIndex = 0;
		bool found = false;

		float entry;
		if (!IntersectAABB(ray, nodes[0].aabbMin, nodes[0].aabbMax, hit.t, entry))
			return false;

		for (;;)
		{
			const GPUBVHNode& node = nodes[nodeIndex];
			if (node.IsLeaf())
			{
				for (uint32_t i = 0; i < node.count; ++i)
				{
					float t, u, v;
					if (IntersectTriangle(ray, triangles[node.leftFirst + i], hit.t, t, u, v))
					{
						hit.t = t;
						hit.u = u;
						hit.v = v;
						hit.instanceIndex = instanceIndex;
						hit.triangleIndex = instance.triangleOffset + node.leftFirst + i;
						found = true;
						if (anyHit)
							return true;
					}
				}
			}
			else
			{
				const uint32_t left = node.leftFirst;
				const uint32_t right = left + 1;
				float tLeft, tRight;
				const bool hitLeft = IntersectAABB(ray, nodes[left].aabbMin, nodes[left].aabbMax, hit.t, tLeft);
				const bool hitRight = IntersectAABB(ray, nodes[right].aabbMin, nodes[right].aabbMax, hit.t, tRight);
				if (hitLeft && hitRight)
				{
					// 先走近的，远的压栈
					const bool leftFirst = tLeft <= tRight;
					stack[stackSize++] = leftFirst ? right : left;
					nodeIndex = leftFirst ? left : right;
					continue;
				}
				if (hitLeft) { nodeIndex = left; continue; }
				if (hitRight) { nodeIndex = right; continue; }
			}

			if (stackSize == 0)
				break;
			nodeIndex = stack[--stackSize];
		}
		return found;
	}
}

// ---------------------------------------------------------------------------

void BuildBVHTree(const BVHAABB* bounds, uint32_t count,
	uint32_t maxLeafSize, uint32_t binCount, uint32_t maxDepth, BVHTree& out)
{
	out.nodes.clear();
	out.primitiveOrder.clear();
	out.depth = 0;
	if (!bounds || count == 0)
		return;

	TreeBuilder builder;
	builder.bounds = bounds;
	builder.maxLeafSize = (std::max)(maxLeafSize, 1u);
	builder.binCount = (std::min)((std::max)(binCount, 2u), kMaxBins);
	builder.maxDepth = (std::min)(maxDepth, kBVHMaxSupportedDepth);
	builder.out = &out;

	builder.centroids.resize(count);
	for (uint32_t i = 0; i < count; ++i)
		builder.centroids[i] = bounds[i].Centroid();

	out.primitiveOrder.resize(count);
	std::iota(out.primitiveOrder.begin(), out.primitiveOrder.end(), 0u);

	// 二叉树、每个叶子至少 1 个图元 → 节点数 <= 2n - 1。
	out.nodes.reserve(static_cast<size_t>(count) * 2);
	out.nodes.emplace_back();
	builder.Subdivide(0, 0, count, 0);
}

uint32_t AppendBLAS(const BVHMeshInput& mesh, const BVHBuildSettings& settings, BVHSceneData& data)
{
	if (data.blasNodes.empty())
		AppendPlaceholderBLAS(data); // 0 号位必须是占位 BLAS

	BVHBLASRange range;
	range.nodeOffset = static_cast<uint32_t>(data.blasNodes.size());
	range.triangleOffset = static_cast<uint32_t>(data.triangles.size());

	const uint32_t vertexCount = static_cast<uint32_t>(mesh.positions.size());
	const size_t sourceTriangles = mesh.indices.size() / 3;
	const bool hasNormals = mesh.normals.size() == mesh.positions.size();
	const bool hasUVs = mesh.uvs.size() == mesh.positions.size();

	// 1) 过滤非法三角形，记录各自的 AABB
	std::vector<uint32_t> source;   // 有效三角形 → 在 indices 中的三角形序号
	std::vector<BVHAABB>  bounds;
	source.reserve(sourceTriangles);
	bounds.reserve(sourceTriangles);
	for (size_t t = 0; t < sourceTriangles; ++t)
	{
		const uint32_t i0 = mesh.indices[t * 3 + 0];
		const uint32_t i1 = mesh.indices[t * 3 + 1];
		const uint32_t i2 = mesh.indices[t * 3 + 2];
		if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount ||
			!IsFinite(mesh.positions[i0]) || !IsFinite(mesh.positions[i1]) || !IsFinite(mesh.positions[i2]))
		{
			++data.stats.skippedTriangles;
			continue;
		}
		BVHAABB box;
		box.Grow(mesh.positions[i0]);
		box.Grow(mesh.positions[i1]);
		box.Grow(mesh.positions[i2]);
		source.push_back(static_cast<uint32_t>(t));
		bounds.push_back(box);
	}

	const uint32_t blasIndex = static_cast<uint32_t>(data.blas.size());
	if (source.empty())
	{
		data.blas.push_back(range);
		return blasIndex;
	}

	// 2) 建树（平铺模式只记录原始顺序和整体包围盒）
	BVHTree tree;
	if (settings.buildTrees)
	{
		BuildBVHTree(bounds.data(), static_cast<uint32_t>(bounds.size()),
			settings.maxLeafSize, settings.binCount, settings.maxBLASDepth, tree);
	}
	else
	{
		tree.primitiveOrder.resize(bounds.size());
		std::iota(tree.primitiveOrder.begin(), tree.primitiveOrder.end(), 0u);
		BVHAABB all;
		for (const BVHAABB& box : bounds)
			all.Grow(box);
		range.bounds = all;
	}

	// 3) 节点原样追加（保持树内局部索引，BLAS 可整体搬移）
	data.blasNodes.insert(data.blasNodes.end(), tree.nodes.begin(), tree.nodes.end());

	// 4) 三角形按叶子顺序写出
	data.triangles.reserve(data.triangles.size() + tree.primitiveOrder.size());
	data.attribs.reserve(data.attribs.size() + tree.primitiveOrder.size());
	for (const uint32_t prim : tree.primitiveOrder)
	{
		const size_t t = source[prim];
		const uint32_t idx[3] = { mesh.indices[t * 3 + 0], mesh.indices[t * 3 + 1], mesh.indices[t * 3 + 2] };

		GPUBVHTriangle tri;
		tri.v0 = mesh.positions[idx[0]];
		tri.v1 = mesh.positions[idx[1]];
		tri.v2 = mesh.positions[idx[2]];
		data.triangles.push_back(tri);

		glm::vec3 geometric = glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0);
		const float geometricLength = glm::length(geometric);
		geometric = geometricLength > 0.0f ? geometric / geometricLength : glm::vec3(0.0f, 1.0f, 0.0f);

		glm::vec3 normals[3];
		for (int k = 0; k < 3; ++k)
		{
			normals[k] = geometric;
			if (hasNormals)
			{
				const glm::vec3& n = mesh.normals[idx[k]];
				const float length = glm::length(n);
				if (IsFinite(n) && length > 1e-12f)
					normals[k] = n / length;
			}
		}

		GPUBVHTriangleAttrib attrib;
		attrib.n0 = glm::vec4(normals[0], 0.0f);
		attrib.n1 = glm::vec4(normals[1], 0.0f);
		attrib.n2 = glm::vec4(normals[2], 0.0f);
		if (hasUVs)
		{
			attrib.uv0 = mesh.uvs[idx[0]];
			attrib.uv1 = mesh.uvs[idx[1]];
			attrib.uv2 = mesh.uvs[idx[2]];
		}
		data.attribs.push_back(attrib);
	}

	range.nodeCount = static_cast<uint32_t>(tree.nodes.size());
	range.triangleCount = static_cast<uint32_t>(tree.primitiveOrder.size());
	range.depth = tree.depth;
	if (!tree.nodes.empty())
	{
		range.bounds.min = tree.nodes[0].aabbMin;
		range.bounds.max = tree.nodes[0].aabbMax;
	}
	data.blas.push_back(range);

	++data.stats.meshCount;
	data.stats.triangleCount += range.triangleCount;
	data.stats.blasNodeCount += range.nodeCount;
	data.stats.blasDepth = (std::max)(data.stats.blasDepth, range.depth);
	return blasIndex;
}

void BuildTLAS(const std::vector<BVHInstanceInput>& instances,
	const BVHBuildSettings& settings, BVHSceneData& data)
{
	data.tlasNodes.clear();
	data.instances.clear();
	data.stats.instanceCount = 0;
	data.stats.tlasNodeCount = 0;
	data.stats.tlasDepth = 0;
	data.stats.skippedInstances = 0;

	if (data.blasNodes.empty())
		AppendPlaceholderBLAS(data);

	std::vector<GPUBVHInstance> valid;
	std::vector<BVHAABB>        bounds;
	valid.reserve(instances.size());
	bounds.reserve(instances.size());

	for (const BVHInstanceInput& input : instances)
	{
		if (input.meshIndex >= data.blas.size() || data.blas[input.meshIndex].triangleCount == 0)
		{
			++data.stats.skippedInstances;
			continue;
		}
		const BVHBLASRange& range = data.blas[input.meshIndex];

		// 缩放为 0 等不可逆变换没有体积，也无法把光线变换回物体空间。
		const float det = glm::determinant(input.objectToWorld);
		if (!std::isfinite(det) || det == 0.0f)
		{
			++data.stats.skippedInstances;
			continue;
		}
		const glm::mat4 worldToObject = glm::inverse(input.objectToWorld);
		const BVHAABB worldBounds = range.bounds.Transformed(input.objectToWorld);
		if (!IsFinite(worldBounds.min) || !IsFinite(worldBounds.max) ||
			!IsFinite(glm::vec3(worldToObject[3])))
		{
			++data.stats.skippedInstances;
			continue;
		}

		GPUBVHInstance instance;
		instance.worldToObject = worldToObject;
		instance.objectToWorld = input.objectToWorld;
		instance.blasNodeOffset = range.nodeOffset;
		instance.triangleOffset = range.triangleOffset;
		instance.materialIndex = input.materialIndex;
		instance.entityID = input.entityID;
		valid.push_back(instance);
		bounds.push_back(worldBounds);
	}

	if (!settings.buildTrees)
	{
		// 平铺：实例保持输入顺序（= Vulkan TLAS 的 instanceIndex），不建 TLAS 树。
		data.instances = std::move(valid);
		data.stats.instanceCount = static_cast<uint32_t>(data.instances.size());
		if (data.instances.empty())
			data.instances.emplace_back(); // 保证 buffer 非空；不会被任何 TLAS 实例引用
		return;
	}

	if (valid.empty())
	{
		// 空场景：一个叶子 → 占位实例 → 占位 BLAS（0 号位）。
		GPUBVHNode leaf;
		leaf.leftFirst = 0;
		leaf.count = 1;
		data.tlasNodes.push_back(leaf);
		data.instances.emplace_back();
		return;
	}

	BVHTree tree;
	BuildBVHTree(bounds.data(), static_cast<uint32_t>(bounds.size()),
		1, settings.binCount, settings.maxTLASDepth, tree);

	data.tlasNodes = std::move(tree.nodes);
	data.instances.reserve(valid.size());
	for (const uint32_t prim : tree.primitiveOrder)
		data.instances.push_back(valid[prim]);

	data.stats.instanceCount = static_cast<uint32_t>(data.instances.size());
	data.stats.tlasNodeCount = static_cast<uint32_t>(data.tlasNodes.size());
	data.stats.tlasDepth = tree.depth;
}

void BuildSceneBVH(const std::vector<BVHMeshInput>& meshes,
	const std::vector<BVHInstanceInput>& instances,
	const BVHBuildSettings& settings, BVHSceneData& out)
{
	out.Clear();
	AppendPlaceholderBLAS(out);
	out.blas.reserve(meshes.size());
	for (const BVHMeshInput& mesh : meshes)
		AppendBLAS(mesh, settings, out);
	BuildTLAS(instances, settings, out);
}

bool BVHRayCast(const BVHSceneData& data, const glm::vec3& origin, const glm::vec3& direction,
	float tMax, BVHHit& hit, bool anyHit)
{
	hit = BVHHit();
	hit.t = tMax;
	if (data.tlasNodes.empty() || data.instances.empty())
		return false;

	const Ray worldRay = MakeRay(origin, direction);
	const GPUBVHNode* nodes = data.tlasNodes.data();

	uint32_t stack[kBVHMaxSupportedDepth + 1];
	uint32_t stackSize = 0;
	uint32_t nodeIndex = 0;
	bool found = false;

	float entry;
	if (!IntersectAABB(worldRay, nodes[0].aabbMin, nodes[0].aabbMax, hit.t, entry))
		return false;

	for (;;)
	{
		const GPUBVHNode& node = nodes[nodeIndex];
		if (node.IsLeaf())
		{
			for (uint32_t i = 0; i < node.count; ++i)
			{
				const uint32_t instanceIndex = node.leftFirst + i;
				const GPUBVHInstance& instance = data.instances[instanceIndex];
				// 方向不归一化：物体空间里的 t 与世界空间的 t 相同。
				const Ray localRay = MakeRay(
					glm::vec3(instance.worldToObject * glm::vec4(origin, 1.0f)),
					glm::vec3(instance.worldToObject * glm::vec4(direction, 0.0f)));
				if (TraverseBLAS(data, instance, instanceIndex, localRay, hit, anyHit))
				{
					found = true;
					if (anyHit)
						return true;
				}
			}
		}
		else
		{
			const uint32_t left = node.leftFirst;
			const uint32_t right = left + 1;
			float tLeft, tRight;
			const bool hitLeft = IntersectAABB(worldRay, nodes[left].aabbMin, nodes[left].aabbMax, hit.t, tLeft);
			const bool hitRight = IntersectAABB(worldRay, nodes[right].aabbMin, nodes[right].aabbMax, hit.t, tRight);
			if (hitLeft && hitRight)
			{
				const bool leftFirst = tLeft <= tRight;
				stack[stackSize++] = leftFirst ? right : left;
				nodeIndex = leftFirst ? left : right;
				continue;
			}
			if (hitLeft) { nodeIndex = left; continue; }
			if (hitRight) { nodeIndex = right; continue; }
		}

		if (stackSize == 0)
			break;
		nodeIndex = stack[--stackSize];
	}

	if (!found)
		hit = BVHHit();
	return found;
}
