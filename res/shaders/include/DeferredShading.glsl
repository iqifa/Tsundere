#ifndef TS_DEFERRED_SHADING_GLSL
#define TS_DEFERRED_SHADING_GLSL

#include "GBuffer.glsl"
#include "PBR.glsl"

// Simple tier: DefaultLit metallic-roughness only, zero shading-model switch.
vec3 EvaluateSimpleLight(Light light, Surface s, float roughness)
{
    return PBREvaluateLight(light, s, roughness);
}

// ClearCoat dual-lobe BRDF * NdotL for a single sampled light direction.
vec3 PBRClearCoatBsdfCos(Surface s, vec3 L, float baseRoughness)
{
    float nv = max(dot(s.N, s.V), 0.0);
    float nl = max(dot(s.N, L), 0.0);
    if (nv <= 0.0 || nl <= 0.0)
        return vec3(0.0);

    vec3 base = PBRBsdfCos(s, L, baseRoughness);

    vec3 H = PBRNormalize(s.V + L, s.N);
    float nh = max(dot(s.N, H), 0.0);
    float vh = max(dot(s.V, H), 0.0);

    const float coatRoughness = 0.08;
    float coatAlpha = PBRAlpha(coatRoughness);
    float coatA2 = coatAlpha * coatAlpha;
    vec3 coatF = PBRFresnel(vec3(0.04), vh);
    vec3 coatSpec = PBRDistribution(nh, coatA2) * PBRMasking(nv, coatA2) * PBRMasking(nl, coatA2) * coatF
                  / max(4.0 * nv, 1e-8);

    return base * (1.0 - coatF) + coatSpec;
}

// Subsurface scattering approximation: standard PBR + wrapped diffuse + back-scatter transmission.
vec3 PBRSubsurfaceContribution(Light light, Surface s, float roughness)
{
    vec3 direct = PBREvaluateLight(light, s, roughness);

    // Compute representative direction & attenuated radiance for wrap/transmission.
    int type = LightType(light);
    vec3 L = vec3(0.0);
    vec3 rad = vec3(0.0);
    if (type == LIGHT_DIRECTIONAL)
    {
        if (dot(light.directionAmbient.xyz, light.directionAmbient.xyz) <= 1e-16)
            return direct;
        L = normalize(-light.directionAmbient.xyz);
        rad = LightRadiance(light);
    }
    else if (type == LIGHT_POINT || type == LIGHT_SPOT)
    {
        vec3 toLight = light.positionType.xyz - s.P;
        float dist = length(toLight);
        if (dist <= 1e-5)
            return direct;
        float atten = DistanceAttenuation(dist, light.params.x, light.params.y);
        if (atten <= 0.0)
            return direct;
        L = toLight / dist;
        rad = LightRadiance(light) * atten;
        if (type == LIGHT_SPOT)
        {
            if (dot(light.directionAmbient.xyz, light.directionAmbient.xyz) <= 1e-16)
                return direct;
            float inner = clamp(light.params.z, 0.0, 89.0);
            float outer = clamp(light.params.w, inner, 89.0);
            float cosInner = cos(radians(inner));
            float cosOuter = cos(radians(outer));
            float theta = dot(-L, normalize(light.directionAmbient.xyz));
            float cone = cosInner - cosOuter > 1e-6 ? smoothstep(cosOuter, cosInner, theta) : step(cosOuter, theta);
            rad *= cone;
        }
    }
    else
    {
        // Area light center approximation for subsurface wrap term.
        vec3 toLight = light.positionType.xyz - s.P;
        float dist = max(length(toLight), 1e-4);
        L = toLight / dist;
        float area = max(light.params.x * max(light.params.y, light.params.x), 1e-4);
        rad = LightRadiance(light) * (area / (dist * dist + area));
    }

    const float wrap = 0.45;
    float ndl = dot(s.N, L);
    float wrapped = max((ndl + wrap) / ((1.0 + wrap) * (1.0 + wrap)), 0.0);
    float extraWrap = max(wrapped - max(ndl, 0.0), 0.0);

    vec3 backDir = normalize(L + s.N * 0.35);
    float backScatter = pow(clamp(dot(s.V, -backDir), 0.0, 1.0), 3.0) * 0.35;

    vec3 sssTint = s.diffuseColor * vec3(1.0, 0.45, 0.30);
    return direct + sssTint * ((extraWrap + backScatter) / PI) * rad;
}

vec3 EvaluateClearCoatLight(Light light, Surface s, float roughness)
{
    uint count = LightType(light) == LIGHT_AREA ? PBR_AREA_SAMPLES : 1u;
    vec3 result = vec3(0.0);
    for (uint i = 0u; i < count; ++i)
    {
        LightSample ls;
        float dist;
        if (PBRSampleDirectLight(light, s, roughness, PBRAreaSample(i), false, ls, dist))
            result += PBRClearCoatBsdfCos(s, ls.L, roughness) * ls.radiance;
        if (LightType(light) == LIGHT_AREA &&
            PBRSampleDirectLight(light, s, roughness, PBRAreaSample(i), true, ls, dist))
            result += PBRClearCoatBsdfCos(s, ls.L, roughness) * ls.radiance;
    }
    return result / float(count);
}

// Complex tier / Uber shader: dispatches across all supported shading models.
vec3 EvaluateComplexLight(Light light, GBufferSurface g, Surface s)
{
    switch (g.shadingModel)
    {
    case TS_CLEAR_COAT:
        return EvaluateClearCoatLight(light, s, g.roughness);
    case TS_SUBSURFACE:
        return PBRSubsurfaceContribution(light, s, g.roughness);
    default:
        return PBREvaluateLight(light, s, g.roughness);
    }
}

#endif // TS_DEFERRED_SHADING_GLSL
