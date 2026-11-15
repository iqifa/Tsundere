// BVH 构建核心的独立测试：只依赖 glm 和 Scene/BVH.{h,cpp}，不需要 T_Core.lib。
// 运行：tests\RunBVHTests.cmd（x64 VS 开发者命令行，仓库根目录）
#include "Scene/BVH.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <string>

namespace
{
	void Require(bool value, const std::string& message)
	{
		if (!value) throw std::runtime_error(message);
	}

	glm::mat4 Translate(const glm::vec3& t)
	{
		glm::mat4 m(1.0f);
		m[3] = glm::vec4(t, 1.0f);
		return m;
	}

	glm::mat4 Scale(const glm::vec3& s)
	{
		glm::mat4 m(1.0f);
		m[0][0] = s.x; m[1][1] = s.y; m[2][2] = s.z;
		return m;
	}

	glm::mat4 RotateY(float radians)
	{
		glm::mat4 m(1.0f);
		const float c = std::cos(radians), s = std::sin(radians);
		m[0][0] = c;  m[0][2] = -s;
		m[2][0] = s;  m[2][2] = c;
		return m;
	}

	// 随机三角形汤：每个三角形独占 3 个顶点。
	BVHMeshInput RandomSoup(uint32_t triangleCount, uint32_t seed, float extent, float size)
	{
		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> position(-extent, extent);
		std::uniform_real_distribution<float> offset(-size, size);
		BVHMeshInput mesh;
		for (uint32_t t = 0; t < triangleCount; ++t)
		{
			const glm::vec3 center(position(rng), position(rng), position(rng));
			for (int k = 0; k < 3; ++k)
			{
				mesh.indices.push_back(static_cast<uint32_t>(mesh.positions.size()));
				mesh.positions.push_back(center + glm::vec3(offset(rng), offset(rng), offset(rng)));
				mesh.normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
				mesh.uvs.push_back(glm::vec2(static_cast<float>(t), static_cast<float>(k)));
			}
		}
		return mesh;
	}

	// 暴力参考：逐实例逐三角形，与 BVH 使用同一份输入数据。
	bool BruteForce(const std::vector<BVHMeshInput>& meshes, const std::vector<BVHInstanceInput>& instances,
		const glm::vec3& origin, const glm::vec3& direction, float tMax, float& tHit)
	{
		bool found = false;
		tHit = tMax;
		for (const auto& instance : instances)
		{
			const BVHMeshInput& mesh = meshes[instance.meshIndex];
			const glm::mat4 inverse = glm::inverse(instance.objectToWorld);
			const glm::vec3 o = glm::vec3(inverse * glm::vec4(origin, 1.0f));
			const glm::vec3 d = glm::vec3(inverse * glm::vec4(direction, 0.0f));
			for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
			{
				const glm::vec3& v0 = mesh.positions[mesh.indices[i]];
				const glm::vec3 e1 = mesh.positions[mesh.indices[i + 1]] - v0;
				const glm::vec3 e2 = mesh.positions[mesh.indices[i + 2]] - v0;
				const glm::vec3 p = glm::cross(d, e2);
				const float det = glm::dot(e1, p);
				if (!(std::fabs(det) > 1e-20f)) continue;
				const float inv = 1.0f / det;
				const glm::vec3 s = o - v0;
				const float u = glm::dot(s, p) * inv;
				if (u < 0.0f || u > 1.0f) continue;
				const glm::vec3 q = glm::cross(s, e1);
				const float v = glm::dot(d, q) * inv;
				if (v < 0.0f || u + v > 1.0f) continue;
				const float t = glm::dot(e2, q) * inv;
				if (t > 0.0f && t < tHit) { tHit = t; found = true; }
			}
		}
		return found;
	}

	struct TreeCheck
	{
		uint32_t leafPrimitives = 0;
		uint32_t maxDepth = 0;
		uint32_t maxLeaf = 0;
		uint32_t visited = 0;
	};

	// 结构不变式：孩子包围盒被父节点包含、叶子区间不重叠且恰好覆盖全部图元。
	template<typename BoundsOf>
	void CheckTree(const GPUBVHNode* nodes, uint32_t nodeCount, uint32_t index, uint32_t depth,
		std::vector<uint8_t>& covered, const BoundsOf& boundsOf, TreeCheck& check)
	{
		Require(index < nodeCount, "node index out of range");
		const GPUBVHNode& node = nodes[index];
		++check.visited;
		check.maxDepth = (std::max)(check.maxDepth, depth);
		const float eps = 1e-4f;
		if (node.IsLeaf())
		{
			check.maxLeaf = (std::max)(check.maxLeaf, node.count);
			for (uint32_t i = 0; i < node.count; ++i)
			{
				const uint32_t prim = node.leftFirst + i;
				Require(prim < covered.size(), "leaf primitive out of range");
				Require(!covered[prim], "primitive referenced by two leaves");
				covered[prim] = 1;
				++check.leafPrimitives;
				const BVHAABB box = boundsOf(prim);
				for (int a = 0; a < 3; ++a)
					Require(box.min[a] >= node.aabbMin[a] - eps && box.max[a] <= node.aabbMax[a] + eps,
						"primitive escapes its leaf bounds");
			}
			return;
		}
		for (uint32_t child = node.leftFirst; child <= node.leftFirst + 1; ++child)
		{
			Require(child < nodeCount && child > index, "bad child index");
			for (int a = 0; a < 3; ++a)
				Require(nodes[child].aabbMin[a] >= node.aabbMin[a] - eps &&
					nodes[child].aabbMax[a] <= node.aabbMax[a] + eps, "child escapes parent bounds");
			CheckTree(nodes, nodeCount, child, depth + 1, covered, boundsOf, check);
		}
	}

	void CheckScene(const BVHSceneData& data, const BVHBuildSettings& settings)
	{
		Require(!data.tlasNodes.empty() && !data.instances.empty() && !data.blasNodes.empty() &&
			!data.triangles.empty() && data.attribs.size() == data.triangles.size(),
			"GPU arrays must never be empty and attribs must parallel triangles");

		// 占位 BLAS 固定在 0 号位
		Require(data.blasNodes[0].IsLeaf() && data.blasNodes[0].count == 1 && data.blasNodes[0].leftFirst == 0,
			"placeholder BLAS missing");

		uint32_t triangles = 0, nodes = 0;
		for (const BVHBLASRange& range : data.blas)
		{
			if (range.triangleCount == 0) continue;
			std::vector<uint8_t> covered(range.triangleCount, 0);
			TreeCheck check;
			const GPUBVHTriangle* tris = data.triangles.data() + range.triangleOffset;
			CheckTree(data.blasNodes.data() + range.nodeOffset, range.nodeCount, 0, 0, covered,
				[&](uint32_t prim) {
					BVHAABB box;
					box.Grow(tris[prim].v0); box.Grow(tris[prim].v1); box.Grow(tris[prim].v2);
					return box;
				}, check);
			Require(check.leafPrimitives == range.triangleCount, "BLAS leaves do not cover all triangles");
			Require(check.visited == range.nodeCount, "BLAS has unreachable nodes");
			Require(check.maxDepth == range.depth && check.maxDepth <= settings.maxBLASDepth, "BLAS depth mismatch");
			Require(check.maxLeaf <= settings.maxLeafSize, "BLAS leaf too large");
			triangles += range.triangleCount;
			nodes += range.nodeCount;
		}
		Require(triangles == data.stats.triangleCount && triangles + 1 == data.triangles.size(), "triangle count mismatch");
		Require(nodes == data.stats.blasNodeCount && nodes + 1 == data.blasNodes.size(), "BLAS node count mismatch");

		if (data.stats.instanceCount == 0)
		{
			Require(data.tlasNodes.size() == 1 && data.tlasNodes[0].count == 1 && data.instances.size() == 1 &&
				data.instances[0].blasNodeOffset == 0 && data.instances[0].triangleOffset == 0,
				"empty scene must point at the placeholder");
			return;
		}

		std::vector<uint8_t> covered(data.instances.size(), 0);
		TreeCheck check;
		CheckTree(data.tlasNodes.data(), static_cast<uint32_t>(data.tlasNodes.size()), 0, 0, covered,
			[&](uint32_t prim) {
				const GPUBVHInstance& instance = data.instances[prim];
				const GPUBVHNode& root = data.blasNodes[instance.blasNodeOffset];
				BVHAABB local; local.min = root.aabbMin; local.max = root.aabbMax;
				return local.Transformed(instance.objectToWorld);
			}, check);
		Require(check.leafPrimitives == data.stats.instanceCount, "TLAS leaves do not cover all instances");
		Require(check.maxLeaf == 1, "TLAS leaves must hold exactly one instance");
		Require(check.maxDepth == data.stats.tlasDepth && check.maxDepth <= settings.maxTLASDepth, "TLAS depth mismatch");
	}

	void CompareWithBruteForce(const std::vector<BVHMeshInput>& meshes, const std::vector<BVHInstanceInput>& instances,
		const BVHSceneData& data, uint32_t rayCount, float extent, uint32_t seed, uint32_t& hits)
	{
		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> position(-extent, extent);
		std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
		hits = 0;
		for (uint32_t i = 0; i < rayCount; ++i)
		{
			const glm::vec3 origin(position(rng), position(rng), position(rng));
			glm::vec3 direction(unit(rng), unit(rng), unit(rng));
			if (i % 7 == 0) direction = glm::vec3(0.0f, i % 2 ? 1.0f : -1.0f, 0.0f); // 轴对齐：invDir 含 inf
			if (glm::dot(direction, direction) < 1e-6f) direction = glm::vec3(1.0f, 0.0f, 0.0f);

			float expected;
			const bool expectedHit = BruteForce(meshes, instances, origin, direction, 1e30f, expected);
			BVHHit hit;
			const bool actualHit = BVHRayCast(data, origin, direction, 1e30f, hit);
			Require(expectedHit == actualHit, "BVH and brute force disagree on hit/miss (ray " + std::to_string(i) + ")");
			if (expectedHit)
			{
				++hits;
				Require(std::fabs(hit.t - expected) <= 1e-3f * (std::max)(1.0f, expected),
					"BVH and brute force disagree on distance (ray " + std::to_string(i) + ")");
				Require(hit.instanceIndex < data.instances.size() && hit.triangleIndex < data.triangles.size(),
					"hit indices out of range");
				BVHHit shadow;
				Require(BVHRayCast(data, origin, direction, 1e30f, shadow, true), "any-hit missed an occluded ray");
			}
		}
	}
}

int main()
{
	try
	{
		BVHBuildSettings settings;

		// 1) 空场景：数组非空、任何光线都不命中
		{
			BVHSceneData data;
			BuildSceneBVH({}, {}, settings, data);
			CheckScene(data, settings);
			BVHHit hit;
			Require(!BVHRayCast(data, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 1e30f, hit),
				"empty scene must not be hit (ray through the placeholder)");
			Require(!BVHRayCast(data, glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(-1.0f, -2.0f, -3.0f), 1e30f, hit),
				"empty scene must not be hit");
		}

		// 2) 单个三角形：命中距离、重心坐标、属性
		{
			BVHMeshInput mesh;
			mesh.positions = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
			mesh.indices = { 0, 1, 2 };
			mesh.uvs = { {0, 0}, {1, 0}, {0, 1} };
			BVHInstanceInput instance;
			instance.objectToWorld = Translate({ 10.0f, 0.0f, 0.0f });
			instance.materialIndex = 7;
			instance.entityID = 42;
			BVHSceneData data;
			BuildSceneBVH({ mesh }, { instance }, settings, data);
			CheckScene(data, settings);
			BVHHit hit;
			Require(BVHRayCast(data, { 10.25f, 0.5f, 3.0f }, { 0, 0, -1 }, 1e30f, hit), "triangle not hit");
			Require(std::fabs(hit.t - 3.0f) < 1e-5f && std::fabs(hit.u - 0.25f) < 1e-5f && std::fabs(hit.v - 0.5f) < 1e-5f,
				"wrong t / barycentrics");
			const GPUBVHInstance& gpuInstance = data.instances[hit.instanceIndex];
			Require(gpuInstance.materialIndex == 7 && gpuInstance.entityID == 42, "instance payload lost");
			const GPUBVHTriangleAttrib& attrib = data.attribs[hit.triangleIndex];
			Require(std::fabs(attrib.n0.z - 1.0f) < 1e-6f, "missing normals must fall back to the geometric normal");
			Require(attrib.uv1.x == 1.0f && attrib.uv2.y == 1.0f, "uv lost");
			// 非归一化方向：t 以方向长度为单位
			Require(BVHRayCast(data, { 10.25f, 0.5f, 3.0f }, { 0, 0, -2 }, 1e30f, hit) && std::fabs(hit.t - 1.5f) < 1e-5f,
				"unnormalized direction must scale t");
			Require(!BVHRayCast(data, { 10.25f, 0.5f, 3.0f }, { 0, 0, -1 }, 2.9f, hit), "tMax not respected");
		}

		// 3) 非法输入：越界索引、NaN、空网格、缩放为 0 的实例
		{
			BVHMeshInput mesh;
			mesh.positions = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {NAN, 0, 0} };
			mesh.indices = { 0, 1, 2,  0, 1, 9,  0, 1, 3,  0, 1 };
			BVHMeshInput empty;
			std::vector<BVHInstanceInput> instances(4);
			instances[1].meshIndex = 1;                       // 空网格
			instances[2].objectToWorld = Scale({ 1, 0, 1 });  // 不可逆
			instances[3].meshIndex = 99;                      // 越界
			BVHSceneData data;
			BuildSceneBVH({ mesh, empty }, instances, settings, data);
			CheckScene(data, settings);
			Require(data.stats.triangleCount == 1 && data.stats.skippedTriangles == 2, "invalid triangles not filtered");
			Require(data.stats.instanceCount == 1 && data.stats.skippedInstances == 3, "invalid instances not filtered");
			Require(data.blas.size() == 2 && data.blas[1].triangleCount == 0, "blas[] must parallel meshes[]");
		}

		// 4) 退化分布：所有三角形完全重合（SAH 无法切分 → 中位切分）
		{
			BVHMeshInput mesh;
			for (uint32_t t = 0; t < 1000; ++t)
			{
				mesh.indices.insert(mesh.indices.end(), { t * 3, t * 3 + 1, t * 3 + 2 });
				mesh.positions.insert(mesh.positions.end(), { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} });
			}
			BVHSceneData data;
			BuildSceneBVH({ mesh }, { BVHInstanceInput() }, settings, data);
			CheckScene(data, settings);
			Require(data.stats.blasDepth <= 9, "coincident triangles must produce a balanced tree");
		}

		// 5) 深度上限：把 maxDepth 压到很小，叶子大小和深度仍然受控
		{
			BVHBuildSettings tight = settings;
			tight.maxBLASDepth = 10; // 4 * 2^10 = 4096 >= 3000
			tight.maxTLASDepth = 6;  // 1 * 2^6  = 64   >= 50
			std::vector<BVHMeshInput> meshes = { RandomSoup(3000, 11, 1.0f, 0.3f) };
			// 极不均匀：大部分挤在一起，少量离群 → SAH 倾向于切出长链
			for (int i = 0; i < 300; ++i) meshes[0].positions[i] *= 1000.0f;
			std::vector<BVHInstanceInput> instances(50);
			for (int i = 0; i < 50; ++i)
				instances[i].objectToWorld = Translate({ static_cast<float>(i * i), 0.0f, 0.0f });
			BVHSceneData data;
			BuildSceneBVH(meshes, instances, tight, data);
			CheckScene(data, tight);
		}

		// 6) 多网格多实例（含旋转 / 非均匀缩放 / 镜像 / 共享网格）：与暴力求交逐光线对比
		{
			std::vector<BVHMeshInput> meshes = {
				RandomSoup(4000, 1, 5.0f, 0.4f),
				RandomSoup(1500, 2, 2.0f, 0.2f),
				RandomSoup(1, 3, 1.0f, 1.0f),
			};
			std::vector<BVHInstanceInput> instances;
			std::mt19937 rng(99);
			std::uniform_real_distribution<float> position(-20.0f, 20.0f);
			std::uniform_real_distribution<float> scale(0.3f, 2.5f);
			std::uniform_real_distribution<float> angle(0.0f, 6.28f);
			for (uint32_t i = 0; i < 24; ++i)
			{
				BVHInstanceInput instance;
				instance.meshIndex = i % 3;
				glm::vec3 s(scale(rng), scale(rng), scale(rng));
				if (i % 5 == 0) s.x = -s.x; // 镜像
				instance.objectToWorld = Translate({ position(rng), position(rng), position(rng) }) *
					RotateY(angle(rng)) * Scale(s);
				instance.entityID = i;
				instances.push_back(instance);
			}
			BVHSceneData data;
			BuildSceneBVH(meshes, instances, settings, data);
			CheckScene(data, settings);
			Require(data.stats.meshCount == 3 && data.stats.instanceCount == 24, "wrong counts");
			Require(data.stats.triangleCount == 5501, "shared meshes must be stored once");

			uint32_t hits = 0;
			CompareWithBruteForce(meshes, instances, data, 3000, 30.0f, 1234, hits);
			Require(hits > 300, "test rays barely hit anything; comparison is not meaningful");
			std::printf("brute-force comparison: 3000 rays, %u hits\n", hits);

			// 只重建 TLAS（以后动态更新的路径）：移动实例后结果仍与暴力一致，BLAS 数据不变
			const size_t blasNodes = data.blasNodes.size(), triangleCount = data.triangles.size();
			for (auto& instance : instances)
				instance.objectToWorld = Translate({ 3.0f, -2.0f, 1.0f }) * instance.objectToWorld;
			BuildTLAS(instances, settings, data);
			CheckScene(data, settings);
			Require(data.blasNodes.size() == blasNodes && data.triangles.size() == triangleCount, "BuildTLAS touched BLAS data");
			CompareWithBruteForce(meshes, instances, data, 1000, 30.0f, 4321, hits);
		}

		// 7) 规模：约 26 万三角形（Sponza 量级）
		{
			std::vector<BVHMeshInput> meshes = { RandomSoup(262144, 5, 50.0f, 0.5f) };
			const auto start = std::chrono::steady_clock::now();
			BVHSceneData data;
			BuildSceneBVH(meshes, { BVHInstanceInput() }, settings, data);
			const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
			CheckScene(data, settings);
			std::printf("262144 tris: %zu BLAS nodes, depth %u, %.1f ms\n",
				data.blasNodes.size(), data.stats.blasDepth, ms);
		}

		std::printf("BVH tests passed\n");
		return 0;
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "BVH tests FAILED: %s\n", e.what());
		return 1;
	}
}
