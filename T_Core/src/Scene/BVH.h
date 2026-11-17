#pragma once

// ---------------------------------------------------------------------------
// BVH.h — 两级 BVH（TLAS / BLAS）的数据结构定义 + 纯 CPU 构建接口
//
// 只依赖 glm，不碰 Scene / RHI，可在任意线程调用、可单独编译测试
// （tests/BVHTests.cpp）。场景采集与 GPU 上传见 BVHBuilder.h。
//
// 结构
//   BLAS  每个 Mesh（子网格）一棵，物体空间，三角形按叶子顺序重排。
//         同一个 Mesh 被多个实体引用时只建一次。
//   TLAS  以实例（实体 × 子网格）的世界 AABB 为图元，叶子恰好 1 个实例。
//
// GPU 侧共 5 个 SSBO（std430，GLSL 镜像见 res/shaders/include/BVH.glsl）：
//   TLASNodes        GPUBVHNode[]            根 = 0
//   Instances        GPUBVHInstance[]        按 TLAS 叶子顺序排列
//   BLASNodes        GPUBVHNode[]            所有 BLAS 首尾相接
//   Triangles        GPUBVHTriangle[]        求交用（热数据）
//   TriangleAttribs  GPUBVHTriangleAttrib[]  着色用（冷数据），与 Triangles 同下标
//
// 之所以拆成 TLAS/BLAS：以后做动态更新时，物体移动只需重建 TLAS + 重传
// Instances（几 KB），BLAS / 三角形数据原样保留。
// ---------------------------------------------------------------------------

#include "Core/Core.h"
#include <glm/glm.hpp>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

constexpr uint32_t kBVHInvalidIndex = 0xFFFFFFFFu;

// 遍历栈的硬上限。构建器保证任何一棵树的深度都不超过 settings.maxDepth，
// 而 maxDepth 会被钳到这个值；着色器里的栈大小按 BVHStats 的实际深度校验即可。
constexpr uint32_t kBVHMaxSupportedDepth = 64;

// ---------------------------------------------------------------------------
// AABB（CPU 侧）
// ---------------------------------------------------------------------------
struct BVHAABB
{
	glm::vec3 min{ FLT_MAX };
	glm::vec3 max{ -FLT_MAX };

	bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

	void Grow(const glm::vec3& p)
	{
		min = (glm::min)(min, p);
		max = (glm::max)(max, p);
	}
	void Grow(const BVHAABB& other)
	{
		min = (glm::min)(min, other.min);
		max = (glm::max)(max, other.max);
	}

	glm::vec3 Extent() const { return max - min; }
	glm::vec3 Centroid() const { return (min + max) * 0.5f; }

	// 表面积的一半；SAH 只比较相对大小，省掉 ×2。
	float HalfArea() const
	{
		if (!IsValid())
			return 0.0f;
		const glm::vec3 e = max - min;
		return e.x * e.y + e.y * e.z + e.z * e.x;
	}

	// 变换 8 个角点后重新取包围盒。
	BVHAABB Transformed(const glm::mat4& m) const
	{
		BVHAABB out;
		if (!IsValid())
			return out;
		for (int i = 0; i < 8; ++i)
		{
			const glm::vec3 corner(
				(i & 1) ? max.x : min.x,
				(i & 2) ? max.y : min.y,
				(i & 4) ? max.z : min.z);
			out.Grow(glm::vec3(m * glm::vec4(corner, 1.0f)));
		}
		return out;
	}
};

// ---------------------------------------------------------------------------
// GPU 结构（std430）。字段顺序/大小改动必须同步 include/BVH.glsl。
// ---------------------------------------------------------------------------

// 32 字节。TLAS 与 BLAS 共用。
//   count == 0 : 内部节点，左孩子 = leftFirst，右孩子 = leftFirst + 1
//   count  > 0 : 叶子，图元区间 [leftFirst, leftFirst + count)
// 索引都是“本棵树内”的局部索引：
//   TLAS  节点 → TLASNodes 下标；图元 → Instances 下标
//   BLAS  节点 → 需加 instance.blasNodeOffset；图元 → 需加 instance.triangleOffset
struct GPUBVHNode
{
	glm::vec3 aabbMin{ 0.0f };
	uint32_t  leftFirst = 0;
	glm::vec3 aabbMax{ 0.0f };
	uint32_t  count = 0;

	bool IsLeaf() const { return count > 0; }
};

// 48 字节。物体空间顶点位置，只放求交需要的数据。
struct GPUBVHTriangle
{
	glm::vec3 v0{ 0.0f }; uint32_t pad0 = 0;
	glm::vec3 v1{ 0.0f }; uint32_t pad1 = 0;
	glm::vec3 v2{ 0.0f }; uint32_t pad2 = 0;
};

// 80 字节。命中后才读取：物体空间法线（xyz，w 未用）+ UV。
struct GPUBVHTriangleAttrib
{
	glm::vec4 n0{ 0.0f };
	glm::vec4 n1{ 0.0f };
	glm::vec4 n2{ 0.0f };
	glm::vec2 uv0{ 0.0f };
	glm::vec2 uv1{ 0.0f };
	glm::vec2 uv2{ 0.0f };
	glm::vec2 pad{ 0.0f };
};

// 144 字节。一个实例 = 一个实体的一个子网格。
struct GPUBVHInstance
{
	glm::mat4 worldToObject{ 1.0f };  // 光线变换到物体空间（方向不要归一化，t 才能直接比较）
	glm::mat4 objectToWorld{ 1.0f };  // 命中点回世界；法线用 transpose(mat3(worldToObject))
	uint32_t  blasNodeOffset = 0;     // 该 BLAS 根节点在 BLASNodes 中的下标
	uint32_t  triangleOffset = 0;     // 该 BLAS 首个三角形在 Triangles/TriangleAttribs 中的下标
	uint32_t  materialIndex = 0;      // 材质缓冲下标（由 BVHBuilder::SetMaterialMap 提供映射）
	uint32_t  entityID = kBVHInvalidIndex; // entt::entity，拾取/调试用
};

static_assert(sizeof(GPUBVHNode) == 32 && offsetof(GPUBVHNode, leftFirst) == 12 &&
	offsetof(GPUBVHNode, aabbMax) == 16 && offsetof(GPUBVHNode, count) == 28,
	"GPUBVHNode must match std430 BVHNode in BVH.glsl");
static_assert(sizeof(GPUBVHTriangle) == 48 && offsetof(GPUBVHTriangle, v1) == 16 &&
	offsetof(GPUBVHTriangle, v2) == 32,
	"GPUBVHTriangle must match std430 BVHTriangle in BVH.glsl");
static_assert(sizeof(GPUBVHTriangleAttrib) == 80 && offsetof(GPUBVHTriangleAttrib, uv0) == 48,
	"GPUBVHTriangleAttrib must match std430 BVHTriangleAttrib in BVH.glsl");
static_assert(sizeof(GPUBVHInstance) == 144 && offsetof(GPUBVHInstance, objectToWorld) == 64 &&
	offsetof(GPUBVHInstance, blasNodeOffset) == 128 && offsetof(GPUBVHInstance, entityID) == 140,
	"GPUBVHInstance must match std430 BVHInstance in BVH.glsl");
static_assert(std::is_trivially_copyable<GPUBVHNode>::value &&
	std::is_trivially_copyable<GPUBVHTriangle>::value &&
	std::is_trivially_copyable<GPUBVHTriangleAttrib>::value &&
	std::is_trivially_copyable<GPUBVHInstance>::value,
	"GPU BVH structs are uploaded with memcpy");

// ---------------------------------------------------------------------------
// 构建输入（CPU 快照，与 Scene / Mesh 解耦，可跨线程传递）
// ---------------------------------------------------------------------------

// 一个网格的物体空间几何。normals / uvs 可以为空或长度不匹配，
// 此时法线退化为几何法线、UV 为 0。
struct BVHMeshInput
{
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<glm::vec2> uvs;
	std::vector<uint32_t>  indices;   // 三角形列表
};

struct BVHInstanceInput
{
	uint32_t  meshIndex = 0;          // 指向 meshes[] / BVHSceneData::blas[]
	glm::mat4 objectToWorld{ 1.0f };
	uint32_t  materialIndex = 0;
	uint32_t  entityID = kBVHInvalidIndex;
};

struct BVHBuildSettings
{
	uint32_t maxLeafSize = 4;    // BLAS 叶子最多几个三角形（TLAS 固定为 1）
	uint32_t binCount = 16;      // SAH 分箱数（2..32）
	uint32_t maxBLASDepth = 32;  // 深度上限 = 着色器遍历栈需求；超限前自动退化为中位切分
	uint32_t maxTLASDepth = 24;

	// false = “平铺”几何：三角形按网格原始顺序写出、不建任何树（blasNodes 只有占位、
	// tlasNodes 为空），实例按输入顺序排列。给硬件光追（Vulkan BLAS/TLAS）使用：
	// 三角形下标就是 BLAS 里的 primitiveID，着色器的表面重建代码因此两后端通用。
	bool buildTrees = true;
};

// ---------------------------------------------------------------------------
// 构建输出
// ---------------------------------------------------------------------------

// 通用构建结果：节点（局部索引）+ 图元重排顺序。
struct BVHTree
{
	std::vector<GPUBVHNode> nodes;          // nodes[0] 为根；count == 0 的输入得到空数组
	std::vector<uint32_t>   primitiveOrder; // 叶子区间引用的是这个数组的下标
	uint32_t                depth = 0;      // 最深叶子的深度（根 = 0）
};

struct BVHBLASRange
{
	uint32_t nodeOffset = 0;
	uint32_t nodeCount = 0;
	uint32_t triangleOffset = 0;
	uint32_t triangleCount = 0;       // 0 = 该网格没有有效三角形，引用它的实例会被跳过
	uint32_t depth = 0;
	BVHAABB  bounds;                  // 物体空间
};

struct BVHStats
{
	uint32_t instanceCount = 0;       // 真实实例数（不含空场景占位）
	uint32_t meshCount = 0;           // 非空 BLAS 数
	uint32_t triangleCount = 0;       // 去重后的三角形数（不含占位）
	uint32_t tlasNodeCount = 0;
	uint32_t blasNodeCount = 0;
	uint32_t tlasDepth = 0;
	uint32_t blasDepth = 0;           // 所有 BLAS 中的最大深度
	uint32_t skippedTriangles = 0;    // 越界索引 / 非有限坐标
	uint32_t skippedInstances = 0;    // 空网格 / 不可逆变换
	float    buildMilliseconds = 0.0f;
};

// 上传到 GPU 的全部数据的 CPU 镜像。
//
// 空场景约定：5 个数组永远非空。BLAS 的 0 号位固定是一个“占位 BLAS”
// （1 个叶子 + 1 个退化三角形，任何光线都打不中）；没有任何实例时，TLAS 是
// 一个指向占位实例的叶子。着色器因此不需要对空场景做任何特判，
// Vulkan 也不会遇到 0 字节 buffer。
struct BVHSceneData
{
	std::vector<GPUBVHNode>           tlasNodes;
	std::vector<GPUBVHInstance>       instances;
	std::vector<GPUBVHNode>           blasNodes;
	std::vector<GPUBVHTriangle>       triangles;
	std::vector<GPUBVHTriangleAttrib> attribs;

	std::vector<BVHBLASRange>         blas;   // 与输入 meshes[] 一一对应
	BVHStats                          stats;

	void Clear() { *this = BVHSceneData(); }
};

// ---------------------------------------------------------------------------
// 构建接口（纯 CPU，线程安全：只读输入、只写 out）
// ---------------------------------------------------------------------------

// 对任意一组 AABB 做分箱 SAH 构建。BLAS 和 TLAS 都走这里。
T_API void BuildBVHTree(const BVHAABB* bounds, uint32_t count,
	uint32_t maxLeafSize, uint32_t binCount, uint32_t maxDepth, BVHTree& out);

// 追加一个网格的 BLAS 到 data（节点、三角形、属性、blas[]）。返回 blas[] 下标。
T_API uint32_t AppendBLAS(const BVHMeshInput& mesh, const BVHBuildSettings& settings, BVHSceneData& data);

// 只重建 TLAS + instances（data.blas 必须已就绪）。以后的动态更新入口。
T_API void BuildTLAS(const std::vector<BVHInstanceInput>& instances,
	const BVHBuildSettings& settings, BVHSceneData& data);

// 完整构建：占位 BLAS + 每个网格的 BLAS + TLAS。settings.buildTrees == false 时见上文“平铺”几何。
T_API void BuildSceneBVH(const std::vector<BVHMeshInput>& meshes,
	const std::vector<BVHInstanceInput>& instances,
	const BVHBuildSettings& settings, BVHSceneData& out);

// ---------------------------------------------------------------------------
// CPU 参考遍历 —— 与着色器将要实现的遍历逻辑一致，用于验证构建结果，
// 也可用于编辑器拾取。direction 不必归一化；t 以 direction 的长度为单位。
// ---------------------------------------------------------------------------
struct BVHHit
{
	float    t = FLT_MAX;
	float    u = 0.0f;                         // 重心坐标：P = (1-u-v)·v0 + u·v1 + v·v2
	float    v = 0.0f;
	uint32_t instanceIndex = kBVHInvalidIndex; // BVHSceneData::instances 下标
	uint32_t triangleIndex = kBVHInvalidIndex; // BVHSceneData::triangles 下标（全局）
};

T_API bool BVHRayCast(const BVHSceneData& data, const glm::vec3& origin, const glm::vec3& direction,
	float tMax, BVHHit& hit, bool anyHit = false);
