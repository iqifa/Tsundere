#ifndef TS_PATH_TRACE_TRACE_RAY_QUERY_GLSL
#define TS_PATH_TRACE_TRACE_RAY_QUERY_GLSL

// 硬件求交适配层：VK_KHR_ray_query 查询 Vulkan TLAS / BLAS。
// 只能从 Vulkan 入口 shader 包含（入口负责 #extension GL_EXT_ray_query）；
// OpenGL 走 SPIRV-Cross 转译，绝不会编译到这里。
#ifndef TS_VULKAN
#error PathTraceTraceRayQuery.glsl is Vulkan-only
#endif

#define BVH_NO_NODE_BUFFERS
#include "PathTraceCommon.glsl"

#define PT_BINDING_TLAS 13
layout(binding = PT_BINDING_TLAS) uniform accelerationStructureEXT u_TLAS;

// 实例全部带 FORCE_OPAQUE + TRIANGLE_FACING_CULL_DISABLE（见 VulkanRayTracingScene），
// 所以不会产生需要着色器确认的候选命中：Proceed 循环结束后 committed hit 即最近有效命中。
// 以后支持 alpha cutout 时，在循环里对 gl_RayQueryCandidateIntersectionTriangleEXT
// 做透明度测试后 rayQueryConfirmIntersectionEXT。
bool PT_IntersectScene(PTRay ray, out PTHit hit)
{
    hit.t = ray.tMax;
    hit.instance = 0u;
    hit.geometry = 0u;
    hit.primitive = 0u;
    hit.bary = vec2(0.0);

    rayQueryEXT q;
    rayQueryInitializeEXT(q, u_TLAS, gl_RayFlagsOpaqueEXT, 0xFFu,
                          ray.origin, ray.tMin, ray.direction, ray.tMax);
    while (rayQueryProceedEXT(q))
    {
    }

    if (rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionTriangleEXT)
        return false;

    hit.t = rayQueryGetIntersectionTEXT(q, true);
    // instanceIndex = TLAS 实例数组下标 = u_BVHInstances 下标（平铺几何按输入顺序排列）
    hit.instance = uint(rayQueryGetIntersectionInstanceIdEXT(q, true));
    hit.geometry = uint(rayQueryGetIntersectionGeometryIndexEXT(q, true));
    hit.primitive = uint(rayQueryGetIntersectionPrimitiveIndexEXT(q, true));
    hit.bary = rayQueryGetIntersectionBarycentricsEXT(q, true);
    return true;
}

bool PT_Occluded(PTRay ray)
{
    rayQueryEXT q;
    rayQueryInitializeEXT(q, u_TLAS,
        gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
        0xFFu, ray.origin, ray.tMin, ray.direction, ray.tMax);
    while (rayQueryProceedEXT(q))
    {
    }
    return rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

#endif
