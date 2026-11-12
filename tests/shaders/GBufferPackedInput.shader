#render_pass gbuffer
#shader vertex
#version 430 core
layout(location=0) in vec2 position;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
}
#shader fragment
#version 430 core
#define TS_GBUFFER_WRITE
#include "../../res/shaders/include/GBuffer.glsl"
// Arbitrary material input: three rows of an already-linear surface palette.
layout(binding=17) uniform sampler2D surfacePalette;
void main() {
    int tile = int(gl_FragCoord.x) / 64;
    vec4 c = texelFetch(surfacePalette, ivec2(tile,0),0);
    vec4 mr = texelFetch(surfacePalette, ivec2(tile,1),0);
    GBufferSurface s = DefaultGBufferSurface();
    s.baseColor = c.rgb; s.ao = c.a;
    s.metallic = mr.r; s.roughness = mr.g;
    s.shadingModel = int(mr.b);
    s.emissive = texelFetch(surfacePalette, ivec2(tile,2),0).rgb;

    vec2 p = vec2(mod(gl_FragCoord.x, 64.0), gl_FragCoord.y) / 32.0 - 1.0;
    if (dot(p,p) > 0.9) discard;
    s.normal = normalize(vec3(p, sqrt(max(1.0-dot(p,p), 0.0))));
    WriteGBuffer(s);
}
