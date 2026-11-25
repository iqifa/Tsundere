#ifndef TS_PATH_TRACE_DIRECT_GLSL
#define TS_PATH_TRACE_DIRECT_GLSL
#include "PathTraceBSDF.glsl"
vec3 PT_DirectLighting(PTSurface s, PTMaterial m, vec3 V, inout uint rng, bool traceShadows) {
    Surface surf = PT_PBRSurface(s,m,V);
    vec3 result = vec3(0);
    for (uint i=0u;i<u_Config.x;++i) {
        Light light = u_Lights[i];
        uint strategies = LightType(light) == LIGHT_AREA ? 2u : 1u;
        for (uint strategy=0u;strategy<strategies;++strategy) {
            vec2 xi = vec2(0.5);
            if (strategies == 2u) xi = vec2(PT_Rand(rng),PT_Rand(rng));
            LightSample lightSample; float distance;
            if (!PBRSampleDirectLight(light,surf,m.roughness,xi,strategy == 1u,lightSample,distance) || dot(lightSample.L,s.geometricNormal) <= 0.0) continue;
            vec3 contribution = PBRShadeLightSample(surf,lightSample,m.roughness);
            if (max(contribution.r,max(contribution.g,contribution.b)) <= 0.0) continue;
            if (traceShadows) {
                PTRay shadow;
                shadow.origin = PT_OffsetOrigin(s.position,s.geometricNormal,lightSample.L);
                shadow.direction = lightSample.L;
                shadow.tMin = 0.0;
                shadow.tMax = distance >= PT_INF ? PT_INF : distance*(1.0-1e-3);
                if (PT_Occluded(shadow)) continue;
            }
            result += contribution;
        }
    }
    return result;
}
#endif
