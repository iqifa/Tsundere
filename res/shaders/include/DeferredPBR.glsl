#ifndef TS_DEFERRED_PBR_GLSL
#define TS_DEFERRED_PBR_GLSL
// Compatibility include. New callers evaluate emitters through PBREvaluateLight.
#include "PBR.glsl"
vec3 ShadeMetallicRoughness(Surface s, LightSample ls, float roughness) {
    return PBRShadeLightSample(s,ls,roughness);
}
#endif
