#ifndef TS_SCENE_TEXTURES_GLSL
#define TS_SCENE_TEXTURES_GLSL

layout(binding = 20) uniform sampler2D u_OpaqueColor;
layout(binding = 21) uniform sampler2D u_OpaqueDepth;

vec2 SceneTextureUV()
{
    return gl_FragCoord.xy / vec2(textureSize(u_OpaqueColor, 0));
}

vec4 SampleOpaqueColor(vec2 uv)
{
    return texture(u_OpaqueColor, uv);
}

// Device depth in [0, 1], comparable to gl_FragCoord.z.
float SampleOpaqueDepth(vec2 uv)
{
    return texture(u_OpaqueDepth, uv).r;
}

#endif
