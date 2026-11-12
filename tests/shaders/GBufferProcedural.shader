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
// No material resources or PBR input declarations.
void main() {
    int tile = int(gl_FragCoord.x) / 64;
    GBufferSurface s = DefaultGBufferSurface();
    s.baseColor = vec3(0.8, 0.2, 0.05);
    s.metallic = tile == 2 || tile == 3 ? 1.0 : 0.0;
    s.roughness = tile == 1 || tile == 3 ? 0.7 : 0.15;
    s.emissive = tile == 4 ? vec3(3.0, 0.5, 0.1) : vec3(0);
    s.shadingModel = tile == 5 ? TS_UNLIT : TS_DEFAULT_LIT;

    vec2 p = vec2(mod(gl_FragCoord.x, 64.0), gl_FragCoord.y) / 32.0 - 1.0;
    if (dot(p,p) > 0.9) discard;
    s.normal = normalize(vec3(p, sqrt(max(1.0-dot(p,p), 0.0))));
    WriteGBuffer(s);
}
