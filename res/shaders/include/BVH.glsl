#ifndef TS_BVH_GLSL
#define TS_BVH_GLSL

// 两级 BVH 的 GPU 数据布局。必须与 T_Core/src/Scene/BVH.h 中的 GPUBVH* 结构保持一致
// （那边有 static_assert 校验大小和偏移）。数据由 BVHBuilder::UploadToGPU() 上传。
//
// 使用方在 #include 之前可以重定义下面的 binding；C++ 侧用
// RHIDescriptorSet::BindStorageBuffer(binding, builder.GetXxxBuffer()) 绑定。

#ifndef BVH_BINDING_TLAS_NODES
#define BVH_BINDING_TLAS_NODES 8
#endif
#ifndef BVH_BINDING_INSTANCES
#define BVH_BINDING_INSTANCES 9
#endif
#ifndef BVH_BINDING_BLAS_NODES
#define BVH_BINDING_BLAS_NODES 10
#endif
#ifndef BVH_BINDING_TRIANGLES
#define BVH_BINDING_TRIANGLES 11
#endif
#ifndef BVH_BINDING_TRIANGLE_ATTRIBS
#define BVH_BINDING_TRIANGLE_ATTRIBS 12
#endif

// 32 字节。TLAS / BLAS 共用。
//   count == 0 : 内部节点，孩子 = leftFirst 与 leftFirst + 1
//   count  > 0 : 叶子，图元区间 [leftFirst, leftFirst + count)
// 索引是树内局部索引：
//   TLAS 节点 → u_TLASNodes[i]；TLAS 图元 → u_BVHInstances[i]（叶子恒为 1 个实例）
//   BLAS 节点 → u_BLASNodes[instance.blasNodeOffset + i]
//   BLAS 图元 → u_BVHTriangles[instance.triangleOffset + i]
struct BVHNode
{
    vec3 aabbMin;
    uint leftFirst;
    vec3 aabbMax;
    uint count;
};

// 144 字节。
struct BVHInstance
{
    mat4 worldToObject;   // 光线 → 物体空间。方向变换后不要归一化，t 才与世界空间一致
    mat4 objectToWorld;   // 命中点 → 世界空间；法线用 transpose(mat3(worldToObject))
    uint blasNodeOffset;
    uint triangleOffset;
    uint materialIndex;
    uint entityID;
};

// 48 字节。物体空间位置。
struct BVHTriangle
{
    vec3 v0; uint pad0;
    vec3 v1; uint pad1;
    vec3 v2; uint pad2;
};

// 80 字节。与 BVHTriangle 同下标，命中后再读。
struct BVHTriangleAttrib
{
    vec4 n0;              // xyz = 物体空间法线
    vec4 n1;
    vec4 n2;
    vec2 uv0;
    vec2 uv1;
    vec2 uv2;
    vec2 pad;
};

layout(std430, binding = BVH_BINDING_TLAS_NODES) readonly buffer BVHTLASNodeBuffer
{
    BVHNode u_TLASNodes[];        // 根 = 0
};

layout(std430, binding = BVH_BINDING_INSTANCES) readonly buffer BVHInstanceBuffer
{
    BVHInstance u_BVHInstances[];
};

layout(std430, binding = BVH_BINDING_BLAS_NODES) readonly buffer BVHBLASNodeBuffer
{
    BVHNode u_BLASNodes[];        // 0 号是占位 BLAS（空场景用）
};

layout(std430, binding = BVH_BINDING_TRIANGLES) readonly buffer BVHTriangleBuffer
{
    BVHTriangle u_BVHTriangles[];
};

layout(std430, binding = BVH_BINDING_TRIANGLE_ATTRIBS) readonly buffer BVHTriangleAttribBuffer
{
    BVHTriangleAttrib u_BVHTriangleAttribs[];
};

// 遍历栈大小下限（BVHBuildSettings 的默认深度上限，实际深度见 BVHStats）：
//   TLAS 栈 >= 24，BLAS 栈 >= 32。
#define BVH_TLAS_STACK_SIZE 24
#define BVH_BLAS_STACK_SIZE 32

#endif
