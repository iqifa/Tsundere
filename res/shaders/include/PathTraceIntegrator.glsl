#ifndef TS_PATH_TRACE_INTEGRATOR_GLSL
#define TS_PATH_TRACE_INTEGRATOR_GLSL

// 共享路径积分器。调用方先包含一个求交适配层，它提供：
//   bool PT_IntersectScene(PTRay ray, out PTHit hit);   // 最近有效命中
//   bool PT_Occluded(PTRay ray);                         // (tMin, tMax) 内有无遮挡
// 一个 invocation = 一个像素；循环 u_Frame.y 个样本，每个样本在 shader 内循环反弹。
//
// 光照分解（保证不重复计数）：
//   * 场景灯光（方向 / 点 / 聚光 / 面光）不是几何体，只通过 NEE + 阴影射线贡献；
//   * 自发光几何只在 BSDF 采样命中时累加（不对它做 NEE）；
//   * 环境只在路径未命中时累加（不对它做 NEE）。
// 发光几何/环境不会与 NEE 重复计数；面光 NEE 内部用面积采样 + GGX 采样的 MIS。
// 所有材质使用共享 GGX + 漫反射混合模型。

#ifndef TS_PATH_TRACE_COMMON_GLSL
#error Include a PathTraceTrace*.glsl adapter before PathTraceIntegrator.glsl
#endif

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#include "PathTraceMaterial.glsl"

#include "PathTraceDirect.glsl"

// ---------------------------------------------------------------------------
// 调试视图（第一次命中）
// ---------------------------------------------------------------------------
vec3 PT_HashColor(uint v)
{
    uint h = PT_Pcg(v + 1u);
    return vec3(float(h & 255u), float((h >> 8) & 255u), float((h >> 16) & 255u)) / 255.0;
}

vec3 PT_DebugColor(uint mode, PTSurface s, PTMaterial m, float t)
{
    if (mode == PT_DEBUG_NORMAL)   return s.shadingNormal * 0.5 + 0.5;
    if (mode == PT_DEBUG_FACING)   return s.frontFace ? vec3(0.1, 0.8, 0.1) : vec3(0.8, 0.1, 0.1);
    if (mode == PT_DEBUG_ALBEDO)   return m.baseColor.rgb;
    if (mode == PT_DEBUG_UV)       return vec3(fract(s.uv), 0.0);
    if (mode == PT_DEBUG_INSTANCE) return PT_HashColor(s.instance);
    if (mode == PT_DEBUG_DEPTH)    return vec3(clamp(t / max(u_Params.z, 1e-3), 0.0, 1.0));
    return vec3(0.0);
}

// ---------------------------------------------------------------------------
// 一条路径
// ---------------------------------------------------------------------------
vec3 PT_TracePath(PTRay ray, inout uint rng)
{
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    uint maxBounces = u_Frame.z;
    uint rrStart = uint(u_Params.y);
    uint debugMode = u_Config.y;

    for (uint bounce = 0u; bounce <= maxBounces; ++bounce)
    {
        PTHit hit;
        if (!PT_IntersectScene(ray, hit))
        {
            if (g_PTTraversalOverflow)
                break;
            radiance += throughput * (debugMode != PT_DEBUG_NONE ? vec3(0.0) : PT_Environment(ray.direction));
            break;
        }

        PTSurface s = PT_ReconstructSurface(hit, ray.direction);
        PTMaterial m = u_Materials[s.material];
        m.baseColor *= PT_SampleBaseColor(m.baseColorTexture, MaterialUV(s.uv, m.tilingOffset));
        vec3 V = -ray.direction;

        if (debugMode == PT_DEBUG_DIRECT || debugMode == PT_DEBUG_DIRECT_SHADOWED)
            return PT_DirectLighting(s, m, V, rng, debugMode == PT_DEBUG_DIRECT_SHADOWED);
        if (debugMode != PT_DEBUG_NONE)
            return PT_DebugColor(debugMode, s, m, hit.t);

        radiance += throughput * m.emission.rgb;

        // 最后一个顶点只收自发光：再往下发射线也不会被统计
        if (bounce == maxBounces)
            break;

        radiance += throughput * PT_DirectLighting(s, m, V, rng, true);

        vec3 L, weight;
        if (!PT_SampleBsdf(m, s, V, rng, L, weight))
            break;
        throughput *= weight;
        if (max(throughput.r, max(throughput.g, throughput.b)) <= 0.0)
            break;

        // 俄罗斯轮盘：存活概率 = 吞吐量最大分量，存活后除以概率保持无偏
        if (bounce >= rrStart)
        {
            float p = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
            if (PT_Rand(rng) >= p)
                break;
            throughput /= p;
        }

        ray.origin = PT_OffsetOrigin(s.position, s.geometricNormal, L);
        ray.direction = L;
        ray.tMin = 0.0;
        ray.tMax = PT_INF;
    }
    return radiance;
}

PTRay PT_CameraRay(vec2 pixel, vec2 jitter, vec2 resolution)
{
    // 像素行号随 NDC.y 增大：与光栅路径写入纹理的方向一致（两后端相同，显示时由 ImGui 翻转 UV）。
    vec2 ndc = (pixel + jitter) / resolution * 2.0 - 1.0;
    vec4 target = u_InvProj * vec4(ndc, 1.0, 1.0);
    vec3 dirView = normalize(target.xyz / target.w);
    PTRay ray;
    ray.origin = u_CameraPos.xyz;
    ray.direction = normalize(mat3(u_InvView) * dirView);
    ray.tMin = 0.0;
    ray.tMax = PT_INF;
    return ray;
}

void main()
{
    uvec2 size = u_Config.zw;
    uvec2 pixel = gl_GlobalInvocationID.xy;
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    uint pixelIndex = pixel.y * size.x + pixel.x;
    uint samples = max(u_Frame.y, 1u);
    float clampLuminance = u_Params.x;

    vec3 sum = vec3(0.0);
    for (uint i = 0u; i < samples; ++i)
    {
        uint rng = PT_InitRng(pixelIndex, u_Frame.x + i, u_Frame.w);
        vec2 jitter = vec2(PT_Rand(rng), PT_Rand(rng));
        PTRay ray = PT_CameraRay(vec2(pixel), jitter, vec2(size));
        vec3 c = PT_TracePath(ray, rng);

        // NaN / Inf 不能进累积缓冲，否则整个像素永久损坏
        if (any(isnan(c)) || any(isinf(c)))
            c = vec3(0.0);
        if (clampLuminance > 0.0)
        {
            float lum = PT_Luminance(c);
            if (lum > clampLuminance)
                c *= clampLuminance / lum;
        }
        sum += c;
        if (g_PTTraversalOverflow)
            break;
    }

    ivec2 coord = ivec2(pixel);
    if (g_PTTraversalOverflow)
    {
        imageStore(u_Accum, coord, vec4(1.0, 0.0, 1.0, 1.0));   // 洋红 = 遍历栈溢出
        return;
    }

    // 线性 HDR 累积：存放到目前为止的平均值（显示阶段再做曝光 / 色调映射）
    float previous = float(u_Frame.x);
    vec3 history = u_Frame.x > 0u ? imageLoad(u_Accum, coord).rgb : vec3(0.0);
    vec3 average = (history * previous + sum) / (previous + float(samples));
    imageStore(u_Accum, coord, vec4(average, 1.0));
}

#endif
