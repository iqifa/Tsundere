#shader compute
#version 450
layout(local_size_x=8, local_size_y=8) in;
layout(binding=0) uniform sampler2D u_Source;
layout(std430, binding=1) writeonly buffer BaseColorPixels { uvec2 pixels[]; };
layout(push_constant) uniform BakePush { uvec4 info; } pc;
#include "include/MaterialSampling.glsl"
void main() {
    uvec2 p = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(p, pc.info.yz))) return;
    // One texel border captures the actual source sampler's edge/border/repeat.
    vec2 uv = (vec2(p) - 0.5) / vec2(pc.info.yz - 2u);
    vec4 c = MaterialDecodeTexel(textureLod(u_Source, uv, 0.0), pc.info.w);
    // Linear RGBA16F precision, two uints per texel: preserves dark sRGB colors
    // without the 4x memory overhead of an RGBA32F cache.
    pixels[pc.info.x + p.y * pc.info.y + p.x] = uvec2(packHalf2x16(c.rg), packHalf2x16(c.ba));
}
