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
// 因此无需 MIS。delta BSDF（光滑金属）跳过 NEE，只走镜面反射。

#ifndef TS_PATH_TRACE_COMMON_GLSL
#error Include a PathTraceTrace*.glsl adapter before PathTraceIntegrator.glsl
#endif

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// ---------------------------------------------------------------------------
// BSDF
// ---------------------------------------------------------------------------
float PT_Alpha(PTMaterial m)
{
    float r = clamp(m.roughness, 0.0, 1.0);
    return r * r;
}

bool PT_IsDelta(PTMaterial m)
{
    return m.type == PT_MATERIAL_METAL && PT_Alpha(m) < 1.6e-3;   // roughness < 0.04
}

float PT_GGX_D(float NoH, float a2)
{
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float PT_SmithG1(float NoX, float a2)
{
    return 2.0 * NoX / (NoX + sqrt(a2 + (1.0 - a2) * NoX * NoX));
}

vec3 PT_Fresnel(vec3 F0, float VoH)
{
    return F0 + (1.0 - F0) * pow(1.0 - clamp(VoH, 0.0, 1.0), 5.0);
}

// f(V, L) · cos(θ_L)，用于 NEE。delta BSDF 返回 0。
vec3 PT_EvalBsdfCos(PTMaterial m, vec3 N, vec3 V, vec3 L)
{
    float NoL = dot(N, L);
    float NoV = dot(N, V);
    if (NoL <= 0.0 || NoV <= 0.0)
        return vec3(0.0);

    if (m.type == PT_MATERIAL_METAL)
    {
        if (PT_IsDelta(m))
            return vec3(0.0);
        float a2 = max(PT_Alpha(m) * PT_Alpha(m), 1e-7);
        vec3 H = normalize(V + L);
        float NoH = max(dot(N, H), 0.0);
        float VoH = max(dot(V, H), 0.0);
        float D = PT_GGX_D(NoH, a2);
        float G = PT_SmithG1(NoV, a2) * PT_SmithG1(NoL, a2);
        vec3 F = PT_Fresnel(m.baseColor.rgb, VoH);
        return D * G * F / (4.0 * NoV);           // (D G F / 4 NoV NoL) · NoL
    }

    return m.baseColor.rgb * (NoL / PI);
}

// 采样出射方向。weight = f · cos / pdf（delta 时为 F）。失败返回 false（路径终止）。
bool PT_SampleBsdf(PTMaterial m, PTSurface s, vec3 V, inout uint rng, out vec3 L, out vec3 weight)
{
    vec3 N = s.shadingNormal;
    float u1 = PT_Rand(rng);
    float u2 = PT_Rand(rng);
    weight = vec3(0.0);
    L = N;

    if (m.type == PT_MATERIAL_METAL)
    {
        float NoV = dot(N, V);
        if (NoV <= 0.0)
            return false;
        if (PT_IsDelta(m))
        {
            L = reflect(-V, N);
            weight = PT_Fresnel(m.baseColor.rgb, NoV);
        }
        else
        {
            float a2 = max(PT_Alpha(m) * PT_Alpha(m), 1e-7);
            float cosTheta = sqrt((1.0 - u1) / (1.0 + (a2 - 1.0) * u1));
            float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
            float phi = 2.0 * PI * u2;
            vec3 t, b;
            PT_Basis(N, t, b);
            vec3 H = normalize(t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + N * cosTheta);
            L = reflect(-V, H);
            float NoL = dot(N, L);
            float NoH = max(dot(N, H), 1e-6);
            float VoH = max(dot(V, H), 1e-6);
            if (NoL <= 0.0)
                return false;
            // pdf(L) = D·NoH / (4·VoH)  →  f·NoL/pdf = F·G·VoH / (NoV·NoH)
            float G = PT_SmithG1(NoV, a2) * PT_SmithG1(NoL, a2);
            weight = PT_Fresnel(m.baseColor.rgb, VoH) * (G * VoH / (NoV * NoH));
        }
    }
    else
    {
        // Lambert：余弦采样，f·cos/pdf = albedo
        L = PT_CosineSample(N, u1, u2);
        weight = m.baseColor.rgb;
    }

    // 出射方向必须在几何面的入射一侧，否则会穿进物体
    return dot(L, s.geometricNormal) > 0.0;
}

// ---------------------------------------------------------------------------
// 直接光照（NEE）：逐个灯光，各发一条阴影射线
// ---------------------------------------------------------------------------
vec3 PT_DirectLighting(PTSurface s, PTMaterial m, vec3 V, inout uint rng)
{
    vec3 result = vec3(0.0);
    uint count = u_Config.x;
    for (uint i = 0u; i < count; ++i)
    {
        Light l = u_Lights[i];
        int type = LightType(l);
        vec3 L;
        float dist;
        vec3 Li = LightRadiance(l);

        if (type == LIGHT_DIRECTIONAL)
        {
            L = normalize(-l.directionAmbient.xyz);
            dist = PT_INF;
        }
        else if (type == LIGHT_AREA)
        {
            // 面光：在光源面上均匀取点，pdf_area = 1 / A；单面，沿 direction 发光。
            vec3 ln = normalize(l.directionAmbient.xyz);
            vec3 R, U;
            BuildLightBasis(ln, R, U);
            float u1 = PT_Rand(rng);
            float u2 = PT_Rand(rng);
            vec3 p;
            float area;
            if (int(l.params.z + 0.5) == AREA_SHAPE_DISK)
            {
                float radius = 0.5 * l.params.x;
                float r = radius * sqrt(u1);
                float phi = 2.0 * PI * u2;
                p = l.positionType.xyz + R * (r * cos(phi)) + U * (r * sin(phi));
                area = PI * radius * radius;
            }
            else
            {
                p = l.positionType.xyz + R * ((u1 - 0.5) * l.params.x) + U * ((u2 - 0.5) * l.params.y);
                area = l.params.x * l.params.y;
            }
            vec3 toL = p - s.position;
            dist = length(toL);
            if (dist <= 1e-5 || area <= 0.0)
                continue;
            L = toL / dist;
            float cosL = dot(-L, ln);
            if (cosL <= 0.0)
                continue;
            Li *= cosL * area / (dist * dist);
        }
        else
        {
            // 点光 / 聚光：衰减与锥角和光栅路径（Lighting.glsl）一致
            vec3 toL = l.positionType.xyz - s.position;
            dist = length(toL);
            if (dist <= 1e-5)
                continue;
            L = toL / dist;
            Li *= DistanceAttenuation(dist, l.params.x);
            if (type == LIGHT_SPOT)
            {
                float cosInner = cos(radians(l.params.z));
                float cosOuter = cos(radians(l.params.w));
                Li *= smoothstep(cosOuter, cosInner, dot(-L, normalize(l.directionAmbient.xyz)));
            }
        }

        if (dot(L, s.geometricNormal) <= 0.0 || max(Li.r, max(Li.g, Li.b)) <= 0.0)
            continue;
        vec3 f = PT_EvalBsdfCos(m, s.shadingNormal, V, L);
        if (max(f.r, max(f.g, f.b)) <= 0.0)
            continue;

        PTRay shadow;
        shadow.origin = PT_OffsetOrigin(s.position, s.geometricNormal, L);
        shadow.direction = L;
        shadow.tMin = 0.0;
        shadow.tMax = dist >= PT_INF ? PT_INF : dist * (1.0 - 1e-3);   // 有限长度阴影射线
        if (!PT_Occluded(shadow))
            result += f * Li;
    }
    return result;
}

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
        vec3 V = -ray.direction;

        if (debugMode != PT_DEBUG_NONE)
            return PT_DebugColor(debugMode, s, m, hit.t);

        radiance += throughput * m.emission.rgb;

        // 最后一个顶点只收自发光：再往下发射线也不会被统计
        if (bounce == maxBounces)
            break;

        if (!PT_IsDelta(m))
            radiance += throughput * PT_DirectLighting(s, m, V, rng);

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
