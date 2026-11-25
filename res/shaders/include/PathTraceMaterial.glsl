#ifndef TS_PATH_TRACE_MATERIAL_GLSL
#define TS_PATH_TRACE_MATERIAL_GLSL
#include "MaterialSampling.glsl"
layout(std430, binding=5) readonly buffer PTBaseColorPixels { uvec2 u_BaseColorPixels[]; };
vec4 PT_BaseColorTexel(uvec4 info, ivec2 p) {
    uvec2 packed = u_BaseColorPixels[info.x + uint(p.y + 1) * (info.y + 2u) + uint(p.x + 1)];
    return vec4(unpackHalf2x16(packed.x), unpackHalf2x16(packed.y));
}
vec4 PT_SampleBaseColor(uvec4 info, vec2 uv) {
    if (info.y == 0u || info.z == 0u) return vec4(1);
    uv.x = (info.w & 4u) != 0u ? fract(uv.x) : clamp(uv.x, -1.0 / float(info.y), 1.0 + 1.0 / float(info.y));
    uv.y = (info.w & 8u) != 0u ? fract(uv.y) : clamp(uv.y, -1.0 / float(info.z), 1.0 + 1.0 / float(info.z));
    vec2 p = clamp(uv * vec2(info.yz) - 0.5, vec2(-1), vec2(info.yz));
    if ((info.w & 2u) != 0u) return PT_BaseColorTexel(info, ivec2(floor(p + 0.5)));
    ivec2 a = ivec2(floor(p)), b = min(a + 1, ivec2(info.yz));
    vec2 f = fract(p);
    return mix(mix(PT_BaseColorTexel(info,a), PT_BaseColorTexel(info,ivec2(b.x,a.y)),f.x),
               mix(PT_BaseColorTexel(info,ivec2(a.x,b.y)), PT_BaseColorTexel(info,b),f.x),f.y);
}
#endif
