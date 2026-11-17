#ifndef TS_PATH_TRACE_TRACE_BVH_GLSL
#define TS_PATH_TRACE_TRACE_BVH_GLSL

// 软件求交适配层：自定义两级 BVH（BVHBuilder 构建、UploadToGPU 上传）。
// OpenGL 后端使用；Vulkan 在设备不支持 Ray Query 时也回退到这里。
// 逻辑与 CPU 参考实现 BVHRayCast（T_Core/src/Scene/BVH.cpp）一一对应。

#include "PathTraceCommon.glsl"

// slab 测试，返回进入距离；区间为 [tMin, tMax]。
bool PT_IntersectAABB(vec3 o, vec3 invD, vec3 bmin, vec3 bmax, float tMin, float tMax, out float tEntry)
{
    vec3 t0 = (bmin - o) * invD;
    vec3 t1 = (bmax - o) * invD;
    vec3 lo = min(t0, t1);
    vec3 hi = max(t0, t1);
    float tNear = max(max(lo.x, lo.y), max(lo.z, tMin));
    float tFar  = min(min(hi.x, hi.y), min(hi.z, tMax));
    tEntry = tNear;
    return tNear <= tFar;
}

// Möller–Trumbore，双面。成功时 t ∈ (tMin, tMax)。
bool PT_IntersectTriangle(vec3 o, vec3 d, BVHTriangle tri, float tMin, float tMax,
                          out float t, out vec2 bary)
{
    vec3 e1 = tri.v1 - tri.v0;
    vec3 e2 = tri.v2 - tri.v0;
    vec3 p = cross(d, e2);
    float det = dot(e1, p);
    t = 0.0;
    bary = vec2(0.0);
    if (!(abs(det) > 1e-20))
        return false;
    float invDet = 1.0 / det;
    vec3 s = o - tri.v0;
    float u = dot(s, p) * invDet;
    if (u < 0.0 || u > 1.0)
        return false;
    vec3 q = cross(s, e1);
    float v = dot(d, q) * invDet;
    if (v < 0.0 || u + v > 1.0)
        return false;
    t = dot(e2, q) * invDet;
    bary = vec2(u, v);
    return t > tMin && t < tMax;
}

// 在一个实例的 BLAS 里找命中。tMax 在命中时缩短。
bool PT_TraverseBLAS(uint instanceIndex, vec3 worldO, vec3 worldD, float tMin,
                     inout float tMax, inout PTHit hit, bool anyHit)
{
    BVHInstance inst = u_BVHInstances[instanceIndex];
    // 方向不归一化：物体空间 t 与世界空间 t 相同（非均匀缩放下依然成立）。
    vec3 o = (inst.worldToObject * vec4(worldO, 1.0)).xyz;
    vec3 d = (inst.worldToObject * vec4(worldD, 0.0)).xyz;
    vec3 invD = 1.0 / d;

    uint base = inst.blasNodeOffset;
    uint stack[BVH_BLAS_STACK_SIZE];
    uint sp = 0u;
    uint node = 0u;
    bool found = false;

    float entry;
    BVHNode root = u_BLASNodes[base];
    if (!PT_IntersectAABB(o, invD, root.aabbMin, root.aabbMax, tMin, tMax, entry))
        return false;

    for (;;)
    {
        BVHNode n = u_BLASNodes[base + node];
        if (n.count > 0u)
        {
            for (uint i = 0u; i < n.count; ++i)
            {
                uint prim = n.leftFirst + i;
                float t;
                vec2 bary;
                if (PT_IntersectTriangle(o, d, u_BVHTriangles[inst.triangleOffset + prim], tMin, tMax, t, bary))
                {
                    tMax = t;
                    hit.t = t;
                    hit.instance = instanceIndex;
                    hit.geometry = 0u;
                    hit.primitive = prim;
                    hit.bary = bary;
                    found = true;
                    if (anyHit)
                        return true;
                }
            }
        }
        else
        {
            uint left = n.leftFirst;
            uint right = left + 1u;
            BVHNode ln = u_BLASNodes[base + left];
            BVHNode rn = u_BLASNodes[base + right];
            float tl, tr;
            bool hl = PT_IntersectAABB(o, invD, ln.aabbMin, ln.aabbMax, tMin, tMax, tl);
            bool hr = PT_IntersectAABB(o, invD, rn.aabbMin, rn.aabbMax, tMin, tMax, tr);
            if (hl && hr)
            {
                if (sp >= uint(BVH_BLAS_STACK_SIZE))
                {
                    g_PTTraversalOverflow = true;
                    return found;
                }
                bool leftNear = tl <= tr;
                stack[sp++] = leftNear ? right : left;
                node = leftNear ? left : right;
                continue;
            }
            if (hl) { node = left; continue; }
            if (hr) { node = right; continue; }
        }

        if (sp == 0u)
            break;
        node = stack[--sp];
    }
    return found;
}

bool PT_TraverseScene(PTRay ray, out PTHit hit, bool anyHit)
{
    hit.t = ray.tMax;
    hit.instance = 0u;
    hit.geometry = 0u;
    hit.primitive = 0u;
    hit.bary = vec2(0.0);

    vec3 invD = 1.0 / ray.direction;
    float tMax = ray.tMax;
    uint stack[BVH_TLAS_STACK_SIZE];
    uint sp = 0u;
    uint node = 0u;
    bool found = false;

    float entry;
    BVHNode root = u_TLASNodes[0];
    if (!PT_IntersectAABB(ray.origin, invD, root.aabbMin, root.aabbMax, ray.tMin, tMax, entry))
        return false;

    for (;;)
    {
        BVHNode n = u_TLASNodes[node];
        if (n.count > 0u)
        {
            for (uint i = 0u; i < n.count; ++i)
            {
                if (PT_TraverseBLAS(n.leftFirst + i, ray.origin, ray.direction, ray.tMin, tMax, hit, anyHit))
                {
                    found = true;
                    if (anyHit)
                        return true;
                }
                if (g_PTTraversalOverflow)
                    return found;
            }
        }
        else
        {
            uint left = n.leftFirst;
            uint right = left + 1u;
            BVHNode ln = u_TLASNodes[left];
            BVHNode rn = u_TLASNodes[right];
            float tl, tr;
            bool hl = PT_IntersectAABB(ray.origin, invD, ln.aabbMin, ln.aabbMax, ray.tMin, tMax, tl);
            bool hr = PT_IntersectAABB(ray.origin, invD, rn.aabbMin, rn.aabbMax, ray.tMin, tMax, tr);
            if (hl && hr)
            {
                if (sp >= uint(BVH_TLAS_STACK_SIZE))
                {
                    g_PTTraversalOverflow = true;
                    return found;
                }
                bool leftNear = tl <= tr;
                stack[sp++] = leftNear ? right : left;
                node = leftNear ? left : right;
                continue;
            }
            if (hl) { node = left; continue; }
            if (hr) { node = right; continue; }
        }

        if (sp == 0u)
            break;
        node = stack[--sp];
    }
    return found;
}

// 最近有效命中
bool PT_IntersectScene(PTRay ray, out PTHit hit)
{
    return PT_TraverseScene(ray, hit, false);
}

// (tMin, tMax) 内是否存在任意遮挡，找到即返回
bool PT_Occluded(PTRay ray)
{
    PTHit hit;
    return PT_TraverseScene(ray, hit, true);
}

#endif
