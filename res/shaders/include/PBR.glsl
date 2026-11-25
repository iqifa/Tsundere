#ifndef TS_PBR_GLSL
#define TS_PBR_GLSL
#include "Lighting.glsl"

vec3 PBRNormalize(vec3 v, vec3 fallback) {
    float len2 = dot(v,v);
    return len2 > 1e-16 ? v * inversesqrt(len2) : fallback;
}
Surface MakePBRSurface(vec3 P, vec3 N, vec3 V, vec3 baseColor, float metallic) {
    Surface s;
    s.P = P;
    s.N = PBRNormalize(N, vec3(0,0,1));
    s.V = PBRNormalize(V, s.N);
    metallic = clamp(metallic, 0.0, 1.0);
    baseColor = clamp(baseColor, 0.0, 1.0);
    s.diffuseColor = baseColor * (1.0-metallic);
    s.specularColor = mix(vec3(0.04), baseColor, metallic);
    s.shininess = 1.0; // unused by GGX; retained for legacy Surface ABI
    return s;
}
float PBRAlpha(float roughness) {
    float r = clamp(roughness, 0.0, 1.0);
    return max(r*r, 0.0025); // same finite highlight at roughness zero in all paths
}
float PBRDistribution(float nh, float a2) {
    float nh2 = clamp(nh*nh, 0.0, 1.0);
    float d = (1.0-nh2) + nh2*a2;
    return a2 / max(PI*d*d, 1e-20);
}
float PBRMasking(float nx, float a2) {
    return 2.0*nx / max(nx + sqrt(a2 + (1.0-a2)*nx*nx), 1e-8);
}
vec3 PBRFresnel(vec3 f0, float vh) {
    return f0 + (1.0-f0)*pow(1.0-clamp(vh,0.0,1.0),5.0);
}
// BRDF * NdotL, in linear radiometric units. Also used for PT sample weights.
vec3 PBRBsdfCos(Surface s, vec3 L, float roughness) {
    float nv = max(dot(s.N,s.V),0.0), nl = max(dot(s.N,L),0.0);
    if (nv <= 0.0 || nl <= 0.0) return vec3(0);
    vec3 H = PBRNormalize(s.V+L,s.N);
    float nh = max(dot(s.N,H),0.0), vh = max(dot(s.V,H),0.0);
    float alpha = PBRAlpha(roughness), a2 = alpha*alpha;
    vec3 F = PBRFresnel(s.specularColor,vh);
    vec3 diffuse = (1.0-F)*s.diffuseColor*(nl/PI);
    vec3 specular = PBRDistribution(nh,a2)*PBRMasking(nv,a2)*PBRMasking(nl,a2)*F / max(4.0*nv,1e-8);
    return diffuse+specular;
}
float PBRGGXPdf(Surface s, vec3 L, float roughness) {
    if (dot(s.N,L) <= 0.0 || dot(s.N,s.V) <= 0.0) return 0.0;
    vec3 H = PBRNormalize(s.V+L,s.N);
    float nh = max(dot(s.N,H),0.0), vh = max(dot(s.V,H),0.0);
    float a = PBRAlpha(roughness);
    return vh > 0.0 ? PBRDistribution(nh,a*a)*nh/(4.0*vh) : 0.0;
}
bool PBRSampleGGX(Surface s, float roughness, vec2 xi, out vec3 L) {
    float a = PBRAlpha(roughness), a2 = a*a;
    float cosTheta = sqrt((1.0-xi.x)/(1.0+(a2-1.0)*xi.x));
    float sinTheta = sqrt(max(0.0,1.0-cosTheta*cosTheta)), phi = 2.0*PI*xi.y;
    vec3 t,b; BuildLightBasis(s.N,t,b);
    vec3 H = normalize(t*(sinTheta*cos(phi))+b*(sinTheta*sin(phi))+s.N*cosTheta);
    L = reflect(-s.V,H);
    return dot(s.V,H) > 0.0 && dot(s.N,L) > 0.0 && dot(s.N,s.V) > 0.0;
}

// One sample of an actual emitter. Area lights use uniform area sampling;
// radiance includes cos(emitter)*area/distance^2 (inverse sampling PDF).
// Punctual lights use the same radius/falloff/spot convention in every path.
bool PBRSampleLight(Light light, Surface s, vec2 xi, out LightSample lightSample, out float distance) {
    distance = 1e30;
    if (light.colorIntensity.a <= 0.0) return false;
    int type = LightType(light);
    if (type == LIGHT_DIRECTIONAL) return SampleDirectional(light,s,lightSample);
    if (type == LIGHT_POINT || type == LIGHT_SPOT) {
        distance = length(light.positionType.xyz-s.P);
        return type == LIGHT_POINT ? SamplePoint(light,s,lightSample) : SampleSpot(light,s,lightSample);
    }
    if (type != LIGHT_AREA || dot(light.directionAmbient.xyz,light.directionAmbient.xyz) <= 1e-16 || light.params.x <= 0.0)
        return false;
    bool disk = int(light.params.z+0.5) == AREA_SHAPE_DISK;
    if (!disk && light.params.y <= 0.0) return false;
    vec3 normal = normalize(light.directionAmbient.xyz), R,U;
    BuildLightBasis(normal,R,U);
    vec3 point = light.positionType.xyz;
    float area;
    if (disk) {
        float radius = light.params.x*0.5;
        float r = radius*sqrt(xi.x), phi = 2.0*PI*xi.y;
        point += R*(r*cos(phi)) + U*(r*sin(phi));
        area = PI*radius*radius;
    } else {
        point += R*((xi.x-0.5)*light.params.x) + U*((xi.y-0.5)*light.params.y);
        area = light.params.x*light.params.y;
    }
    vec3 delta = point-s.P;
    distance = length(delta);
    if (distance <= 1e-5) return false;
    lightSample.L = delta/distance;
    float cosine = max(dot(normal,-lightSample.L),0.0);
    if (cosine <= 0.0) return false;
    lightSample.specL = lightSample.L;
    lightSample.diffuseTerm = max(dot(s.N,lightSample.L),0.0);
    lightSample.specularTerm = 1.0;
    lightSample.radiance = LightRadiance(light)*(cosine*area/(distance*distance));
    return lightSample.diffuseTerm > 0.0;
}
vec3 PBRShadeLightSample(Surface s, LightSample lightSample, float roughness) {
    return PBRBsdfCos(s,lightSample.L,roughness)*lightSample.radiance;
}

// Area-light NEE combines uniform emitter sampling and GGX sampling with the
// balance heuristic (one sample of each). Narrow highlights therefore converge
// without requiring the raster quadrature to land on the tiny specular peak.
bool PBRSampleDirectLight(Light light, Surface s, float roughness, vec2 xi, bool specularStrategy,
                         out LightSample lightSample, out float distance) {
    if (LightType(light) != LIGHT_AREA) return PBRSampleLight(light,s,xi,lightSample,distance);
    bool disk = int(light.params.z+0.5) == AREA_SHAPE_DISK;
    if (light.colorIntensity.a <= 0.0 || light.params.x <= 0.0 || (!disk && light.params.y <= 0.0) ||
        dot(light.directionAmbient.xyz,light.directionAmbient.xyz) <= 1e-16) return false;
    vec3 normal = normalize(light.directionAmbient.xyz);
    float area = disk ? PI*light.params.x*light.params.x*0.25 : light.params.x*light.params.y;
    if (specularStrategy) {
        if (!PBRSampleGGX(s,roughness,xi,lightSample.L)) return false;
        float cosine = dot(normal,-lightSample.L);
        if (cosine <= 1e-8) return false;
        distance = dot(light.positionType.xyz-s.P,normal)/dot(lightSample.L,normal);
        if (distance <= 1e-5) return false;
        vec3 R,U; BuildLightBasis(normal,R,U);
        vec3 delta = s.P+distance*lightSample.L-light.positionType.xyz;
        vec2 uv = vec2(dot(delta,R),dot(delta,U));
        if (disk ? dot(uv,uv) > light.params.x*light.params.x*0.25 :
            any(greaterThan(abs(uv),light.params.xy*0.5))) return false;
        lightSample.specL = lightSample.L;
        lightSample.diffuseTerm = max(dot(s.N,lightSample.L),0.0);
        lightSample.specularTerm = 1.0;
    } else if (!PBRSampleLight(light,s,xi,lightSample,distance)) return false;
    float lightPdf = distance*distance / max(dot(normal,-lightSample.L)*area,1e-20);
    float specPdf = PBRGGXPdf(s,lightSample.L,roughness);
    // For either strategy: Li / pdf * pdf/(lightPdf+specPdf).
    lightSample.radiance = LightRadiance(light)/max(lightPdf+specPdf,1e-20);
    return true;
}

// Deterministic Hammersley quadrature for rasterization. PT uses the same
// integrand with random samples; no representative-point or extra /PI factor.
const uint PBR_AREA_SAMPLES = 64u;
vec2 PBRAreaSample(uint index) {
    return vec2((float(index)+0.5)/float(PBR_AREA_SAMPLES),
                float(bitfieldReverse(index))*2.3283064365386963e-10);
}
vec3 PBREvaluateLight(Light light, Surface s, float roughness) {
    uint count = LightType(light) == LIGHT_AREA ? PBR_AREA_SAMPLES : 1u;
    vec3 result = vec3(0);
    for (uint i=0u;i<count;++i) {
        LightSample lightSample; float distance;
        if (PBRSampleDirectLight(light,s,roughness,PBRAreaSample(i),false,lightSample,distance))
            result += PBRShadeLightSample(s,lightSample,roughness);
        if (LightType(light) == LIGHT_AREA && PBRSampleDirectLight(light,s,roughness,PBRAreaSample(i),true,lightSample,distance))
            result += PBRShadeLightSample(s,lightSample,roughness);
    }
    return result/float(count);
}
#endif
