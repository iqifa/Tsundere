#shader vertex
#version 450 core
layout(location=0) out vec2 uv;
void main() {
#ifdef TS_VULKAN
    int vertex = gl_VertexIndex;
#else
    int vertex = gl_VertexID;
#endif
    uv = vec2((vertex << 1) & 2, vertex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0, 1);
}
#shader fragment
#version 450 core
#include "include/SkyboxSampling.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(binding=10) uniform sampler2D sceneColor;
layout(binding=11) uniform sampler2D sceneDepth;
layout(binding=12) uniform samplerCube sky;
layout(std140,binding=0) uniform SkyCamera { mat4 inverseSkyViewProj; };
void main() {
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), textureSize(sceneColor,0)-1);
    vec4 surface = texelFetch(sceneColor,pixel,0);
    float coverage = clamp(surface.a,0.0,1.0);
    float depth = texelFetch(sceneDepth,pixel,0).r;
    // Fully covered geometry never samples the sky. At silhouettes depth alone
    // cannot represent MSAA/TAA coverage, including partial coverage at depth 1.
    if (depth < 1.0 && coverage >= 1.0) {
        color = vec4(surface.rgb,1);
        return;
    }
    vec4 direction = inverseSkyViewProj * vec4(uv * 2.0 - 1.0,1,1);
    color = vec4(surface.rgb + (1.0-coverage) * SampleSkyboxLinear(sky,direction.xyz),1);
}
