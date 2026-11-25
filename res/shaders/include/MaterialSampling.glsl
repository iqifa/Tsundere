#ifndef TS_MATERIAL_SAMPLING_GLSL
#define TS_MATERIAL_SAMPLING_GLSL
// Color textures are sRGB; material tint and floating-point textures are linear.
// UNORM file textures need explicit decoding; SRGB textures are decoded by hardware.
vec3 MaterialSRGBToLinear(vec3 c) {
    return mix(c / 12.92, pow(max((c + 0.055) / 1.055, vec3(0)), vec3(2.4)),
               step(vec3(0.04045), c));
}
vec4 MaterialDecodeTexel(vec4 c, uint flags) {
    if ((flags & 1u) != 0u) c.rgb = MaterialSRGBToLinear(c.rgb);
    return c; // Alpha is always linear.
}
vec2 MaterialUV(vec2 uv, vec4 tilingOffset) {
    return uv * tilingOffset.xy + tilingOffset.zw;
}
// Level zero is shared with PT (which has no raster derivatives). Decode BEFORE
// filtering, matching a hardware sRGB sampler, including wrap/border behavior.
vec4 MaterialSampleBaseColor(sampler2D tex, vec2 uv, uint flags) {
    if ((flags & 1u) == 0u || (flags & 2u) != 0u)
        return MaterialDecodeTexel(textureLod(tex, uv, 0.0), flags);
    vec2 size = vec2(textureSize(tex, 0));
    vec2 p = uv * size - 0.5;
    vec2 f = fract(p);
    vec2 origin = (floor(p) + 0.5) / size;
    vec4 a = MaterialDecodeTexel(textureLod(tex, origin, 0.0), flags);
    vec4 b = MaterialDecodeTexel(textureLod(tex, origin + vec2(1,0) / size, 0.0), flags);
    vec4 c = MaterialDecodeTexel(textureLod(tex, origin + vec2(0,1) / size, 0.0), flags);
    vec4 d = MaterialDecodeTexel(textureLod(tex, origin + vec2(1,1) / size, 0.0), flags);
    return mix(mix(a,b,f.x), mix(c,d,f.x), f.y);
}
#endif
