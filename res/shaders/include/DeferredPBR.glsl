#ifndef TS_DEFERRED_PBR_GLSL
#define TS_DEFERRED_PBR_GLSL
// GGX, Smith masking, Schlick Fresnel. Area lights retain the existing
// representative-point approximation and integrated diffuse form factor.
vec3 ShadeMetallicRoughness(Surface s, LightSample ls, float roughness) {
    float nv = max(dot(s.N, s.V), 0.0);
    float nl = max(dot(s.N, ls.specL), 0.0);
    if (nv <= 0.0) return vec3(0);
    vec3 hsum = s.V + ls.specL;
    vec3 H = hsum * inversesqrt(max(dot(hsum, hsum), 1e-8));
    float nh = max(dot(s.N, H), 0.0);
    float vh = max(dot(s.V, H), 0.0);
    float alpha = max(roughness * roughness, 0.0025);
    float a2 = alpha * alpha;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    float D = a2 / max(PI * d * d, 1e-12);
    float gv = 2.0 * nv / max(nv + sqrt(a2 + (1.0-a2)*nv*nv), 1e-6);
    float gl = 2.0 * nl / max(nl + sqrt(a2 + (1.0-a2)*nl*nl), 1e-6);
    vec3 F = s.specularColor + (1.0-s.specularColor) * pow(1.0-vh, 5.0);
    vec3 diffuse = (1.0-F) * s.diffuseColor / PI * ls.diffuseTerm;
    vec3 specular = D * gv * gl * F / max(4.0*nv*nl, 1e-6);
    return (diffuse + specular * nl * ls.specularTerm) * ls.radiance;
}
#endif
