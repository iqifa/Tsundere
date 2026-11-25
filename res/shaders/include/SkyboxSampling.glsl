#ifndef TS_SKYBOX_SAMPLING_GLSL
#define TS_SKYBOX_SAMPLING_GLSL

// SkyBox creates RGBA8_UNORM cubemaps containing sRGB bytes on both backends.
// Decode once here; all scene color targets (including PT) contain linear light.
// Display encoding belongs to the common post-process, not the sky pass.
// Keep LOD/filtering identical to PT, which has no raster derivatives.
vec3 SampleSkyboxLinear(samplerCube sky, vec3 direction)
{
    vec3 c = textureLod(sky, direction, 0.0).rgb;
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)),
               step(vec3(0.04045), c));
}

#endif
