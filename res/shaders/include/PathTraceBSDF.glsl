#ifndef TS_PATH_TRACE_BSDF_GLSL
#define TS_PATH_TRACE_BSDF_GLSL
#include "PBR.glsl"

Surface PT_PBRSurface(PTSurface s, PTMaterial m, vec3 V) {
    return MakePBRSurface(s.position,s.shadingNormal,V,m.baseColor.rgb,m.metallic);
}
vec3 PT_EvalBsdfCos(PTMaterial m, vec3 N, vec3 V, vec3 L) {
    return PBRBsdfCos(MakePBRSurface(vec3(0),N,V,m.baseColor.rgb,m.metallic),L,m.roughness);
}
float PT_SpecularProbability(PTMaterial m) {
    return mix(0.5,1.0,clamp(m.metallic,0.0,1.0));
}
float PT_BsdfPdf(PTMaterial m, Surface s, vec3 L) {
    float nl = max(dot(s.N,L),0.0);
    if (nl <= 0.0 || dot(s.N,s.V) <= 0.0) return 0.0;
    float specPdf = PBRGGXPdf(s,L,m.roughness);
    return mix(nl/PI,specPdf,PT_SpecularProbability(m));
}
// Sample the diffuse/GGX mixture and divide the COMPLETE BSDF by its mixture
// PDF. Metallic is continuous; dielectrics retain their 4% specular lobe.
// No delta shortcut: raster and PT use the same minimum GGX roughness.
bool PT_SampleBsdf(PTMaterial m, PTSurface s, vec3 V, inout uint rng, out vec3 L, out vec3 weight) {
    Surface surf = PT_PBRSurface(s,m,V);
    weight = vec3(0); L = surf.N;
    if (dot(surf.N,surf.V) <= 0.0) return false;
    float choose = PT_Rand(rng), u1 = PT_Rand(rng), u2 = PT_Rand(rng);
    if (choose < PT_SpecularProbability(m)) {
        if (!PBRSampleGGX(surf,m.roughness,vec2(u1,u2),L)) return false;
    } else {
        L = PT_CosineSample(surf.N,u1,u2);
    }
    if (dot(L,s.geometricNormal) <= 0.0) return false;
    float pdf = PT_BsdfPdf(m,surf,L);
    if (pdf <= 0.0) return false;
    weight = PBRBsdfCos(surf,L,m.roughness)/pdf;
    return true;
}
#endif
