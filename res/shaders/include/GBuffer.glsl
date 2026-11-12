#ifndef TS_GBUFFER_GLSL
#define TS_GBUFFER_GLSL
// ABI v1, linear attachments, no sRGB render targets:
// 0 RGBA16F: signed world unit normal + integer shading ID (0 lit, 1 unlit).
// 1 RGBA8_UNORM: linear baseColor + AO. 2 RG16F: metallic + perceptual roughness.
// 3 RG16F: (current - previous) unjittered NDC.xy * 0.5.
// 4 RGBA16F: linear HDR emissive RGB + reserved zero alpha.
// Depth D24S8: device [0,1], reconstruct using inverse jittered VP.
const int TS_DEFAULT_LIT = 0;
const int TS_UNLIT = 1;
struct GBufferSurface {
    vec3 normal; int shadingModel;
    vec3 baseColor; float ao;
    float metallic; float roughness;
    vec2 velocity; vec3 emissive;
};
vec3 GBufferWorldPosition(vec2 uv, float depth, mat4 inverseViewProjection) {
#ifdef TS_VULKAN
    float clipDepth = depth;
#else
    float clipDepth = depth * 2.0 - 1.0;
#endif
    vec4 world = inverseViewProjection * vec4(uv * 2.0 - 1.0, clipDepth, 1.0);
    return world.xyz / world.w;
}
GBufferSurface DefaultGBufferSurface() {
    return GBufferSurface(vec3(0,0,1), TS_DEFAULT_LIT, vec3(1), 1.0,
                          0.0, 0.5, vec2(0), vec3(0));
}
#ifdef TS_GBUFFER_WRITE
layout(location=0) out vec4 o_Normal;
layout(location=1) out vec4 o_BaseColorAO;
layout(location=2) out vec2 o_MetallicRoughness;
layout(location=3) out vec2 o_Velocity;
layout(location=4) out vec4 o_Emissive;
void WriteGBuffer(GBufferSurface s) {
    o_Normal = vec4(normalize(s.normal), float(s.shadingModel));
    o_BaseColorAO = vec4(clamp(s.baseColor, 0.0, 1.0), clamp(s.ao, 0.0, 1.0));
    o_MetallicRoughness = clamp(vec2(s.metallic, s.roughness), 0.0, 1.0);
    o_Velocity = s.velocity;
    o_Emissive = vec4(max(s.emissive, vec3(0)), 0);
}
#endif
#ifdef TS_GBUFFER_READ
layout(binding=10) uniform sampler2D u_GBufferNormal;
layout(binding=11) uniform sampler2D u_GBufferBaseColorAO;
layout(binding=12) uniform sampler2D u_GBufferMetallicRoughness;
layout(binding=14) uniform sampler2D u_GBufferEmissive;
layout(binding=15) uniform sampler2D u_GBufferVelocity;
GBufferSurface ReadGBuffer(ivec2 pixel) {
    GBufferSurface s = DefaultGBufferSurface();
    vec4 n = texelFetch(u_GBufferNormal, pixel, 0);
    vec4 c = texelFetch(u_GBufferBaseColorAO, pixel, 0);
    vec2 mr = texelFetch(u_GBufferMetallicRoughness, pixel, 0).rg;
    s.normal = normalize(n.xyz); s.shadingModel = int(round(n.w));
    s.baseColor = c.rgb; s.ao = c.a;
    s.metallic = mr.x; s.roughness = mr.y;
    s.emissive = texelFetch(u_GBufferEmissive, pixel, 0).rgb;
    s.velocity = texelFetch(u_GBufferVelocity, pixel, 0).rg;
    return s;
}
#endif
#endif
