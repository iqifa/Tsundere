# Deferred GBuffer ABI v1

`res/shaders/include/GBuffer.glsl` is the shared writer/reader contract. A material
uses `#render_pass gbuffer`, defines `TS_GBUFFER_WRITE`, includes it in its fragment
stage, then calls `WriteGBuffer`. Inputs are independent of this output contract.
`DefaultGBufferSurface()` supplies AO 1, emissive 0, metallic 0, roughness 0.5,
white base color, +Z normal, zero velocity and default_lit. Set the actual normal
and velocity for mesh geometry. If using the shader directive
`#shading_model unlit`, assign `surface.shadingModel = TS_SHADING_MODEL` before writing;
a shader may also choose the supported model per pixel.

| Location | GLSL output | Attachment | Encoding |
| --- | --- | --- | --- |
| 0 | vec4 | RGBA16F | Signed normalized world normal RGB; alpha is exact float integer model ID, 0 default_lit / 1 unlit |
| 1 | vec4 | RGBA8_UNORM | Linear baseColor RGB [0,1], AO [0,1]; no sRGB target conversion |
| 2 | vec2 | RG16F | Metallic and perceptual roughness [0,1] |
| 3 | vec2 | RG16F | `(currentUnjitteredNDC.xy - previousUnjitteredNDC.xy) * 0.5`, unchanged sign/units |
| 4 | vec4 | RGBA16F | Linear HDR emissive RGB, nonnegative; alpha reserved 0 |
| depth | depth attachment | D24_UNORM_S8_UINT | Device depth [0,1], clear 1 |

The compiler rejects missing, extra or incorrectly typed fragment outputs.
Lighting uses exact texel fetches, so normals, material IDs and parameters do not
blend across pixels. Position uses the inverse jittered view-projection matrix;
the shared reader converts device depth to OpenGL NDC [-1,1] or Vulkan NDC [0,1]
as appropriate before applying that inverse. Depth 1 is background.

## Lighting

One fullscreen draw selects the model per pixel. default_lit uses GGX distribution,
Smith masking and Schlick Fresnel, with dielectric F0 0.04, metallic F0 from
baseColor and diffuse reduced by `(1-metallic)*(1-F)`. Perceptual roughness is
squared, with alpha limited to 0.0025 to keep a zero-roughness lobe finite.
Directional, point and spot lights use the existing light sampling. Area lights
retain the existing representative-point / integrated form-factor approximation.
This is not an LTC area-light implementation.

The existing ambient interface remains: the first directional light supplies
`Color * Intensity * Ambient`. Ambient is multiplied by baseColor, `(1-metallic)`
and AO. AO does not attenuate direct light or emissive. There is no IBL in this
change. Emissive is added once regardless of light count. unlit returns exactly
`baseColor + emissive`, independent of ambient, AO and lights. Output is linear HDR;
tone mapping remains a later pass.

## Material resources

There is no required PBR input list, texture name, or material block. A shader may
sample and mix arbitrary inputs, use constants, or generate colors procedurally.
Current material resource support is set 0, sampler2D at binding >= 10, and one or
more material UBOs at binding >= 3. UBO and sampler bindings must not collide.
Parameters are packed by reflected member names/offsets; textures are looked up by
reflected sampler name and bound at its declared binding. The material parameter
system supports float, int and vec2/3/4; this change does not introduce material
SSBOs, storage images, arrays, cubemaps or additional descriptor sets. Missing
textures retain the existing white fallback. Surface defaults belong to the shader.

Engine bindings are reserved: frame at 1, camera at 2; draw transforms use push
constants. GBuffer shaders may omit all of these. If declared, camera is the three
mat4 values jitteredViewProj/viewProj/prevViewProj at offsets 0/64/128 (192 bytes),
and push constants are model/prevModel at 0/64 (128 bytes). GBuffer frame is a
16-byte block with hasNormalMap int at offset 0. The built-in normal-map presence
flag refers specifically to texture_normal1; arbitrary textures at binding 12 do
not activate it. Engine scene/shadow resources keep their reserved semantic names.

## Built-in and legacy compatibility

The built-in shader writes the same ABI and consumes the existing base color,
roughness, metallic, emissive color/strength, UV and normal inputs. Base color
parameters and emissive parameters are linear. The legacy imported diffuse-map
convention is sRGB bytes in an UNORM texture; the built-in shader decodes these
bytes once. Do not bind an already hardware-decoded sRGB texture to this legacy
sampler. Custom shaders choose their own texture color-space handling and must
write linear output. Normal and metallic/roughness data are not color data.

Legacy `specular` textures are deliberately ignored by the deferred compatibility
path: colored specular cannot be converted uniquely to metallic. No specular,
metallic or roughness map is guessed to stand in for another. Explicit u_Metallic
is used when present, otherwise 0 (dielectric); u_Roughness defaults to 0.5.
Missing built-in parameters are filled from the built-in defaults instead of
zeroing tint or UV scale. This changes the previous Blinn-Phong appearance; it is
not a lossless material conversion. Artist-authored specular-workflow conversion
can be implemented in a custom GBuffer shader.

## Reproducible acceptance

Run `tests\RunDeferredGBufferTests.cmd` from the repository root (Visual Studio
2026 and Vulkan SDK; VULKAN_SDK may override the default SDK path). The test compiles
both OpenGL and Vulkan targets and renders with a hidden OpenGL 4.3 context using
the real shared GBuffer writer and DeferredLight shader. It rejects invalid ABI
outputs and checks a shader with no material resources against a shader using an
arbitrarily named packed palette sampler at binding 17. Both generate identical
surface data; full-image HDR lighting error must be below 0.001 (observed: 0).

`build/gbuffer-acceptance.ppm` contains three rows: procedural inputs with a light,
packed texture inputs with the same light, and procedural inputs without lights.
Columns: dielectric roughness .15 / .7; metal roughness .15 / .7; HDR emissive;
unlit. The assertions check metal and roughness responses, emissive >1 surviving
without lights, unlit light independence, finite output and input equivalence.
The image uses Reinhard plus display gamma only for inspection. This is an
OpenGL GPU render test plus Vulkan shader compilation, not a Vulkan runtime test.
