#shader vertex
#version 450 core

layout(push_constant) uniform PostProcessSettings
{
    vec4 settings;
    vec4 controls;
} pc;

layout(location = 0) out vec2 v_UV;
layout(location = 1) flat out vec4 v_Settings;
layout(location = 2) flat out vec4 v_Controls;

void main()
{
    // This stage targets Vulkan SPIR-V on both backends due to the push constant.
    v_UV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(v_UV * 2.0 - 1.0, 0.0, 1.0);
    v_Settings = pc.settings;
    v_Controls = pc.controls;
}

#shader fragment
#version 450 core

layout(location = 0) in vec2 v_UV;
layout(location = 1) flat in vec4 v_Settings;
layout(location = 2) flat in vec4 v_Controls;
layout(location = 0) out vec4 o_Color;

layout(binding = 0) uniform sampler2D u_Input;
layout(binding = 1) uniform sampler2D u_Auxiliary;

// Effect IDs shared with FullscreenPass::Effect.
const int Prefilter = 0;
const int Gaussian = 1;
const int Composite = 2;
const int DepthOfField = 3;
const int MotionBlur = 4;
const int ToneMapping = 5;
const int ColorGrading = 6;
const int Sharpen = 7;
const int ChromaticAberration = 8;
const int Vignette = 9;
const int FXAA = 10;

vec3 ExtractHighlights(vec3 color)
{
    float threshold = v_Controls.x;
    float knee = max(v_Controls.y, 0.0001);
    float brightness = max(color.r, max(color.g, color.b));
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    return color * (max(brightness - threshold, soft) / max(brightness, 0.0001));
}

vec4 GaussianBlur(vec2 uv, vec2 stepUV)
{
    // Linear sampling combines each pair of taps in the 9-tap kernel.
    vec4 color = texture(u_Input, uv) * 0.227027;
    color += (texture(u_Input, uv + stepUV * 1.384615) +
              texture(u_Input, uv - stepUV * 1.384615)) * 0.316216;
    color += (texture(u_Input, uv + stepUV * 3.230769) +
              texture(u_Input, uv - stepUV * 3.230769)) * 0.070270;
    return color;
}

float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

float ViewDepth(vec2 uv)
{
    ivec2 size = textureSize(u_Auxiliary, 0);
    ivec2 pixel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    float depth = texelFetch(u_Auxiliary, pixel, 0).r;
#ifndef TS_VULKAN
    depth = depth * 2.0 - 1.0;
#endif
    // For the perspective matrix, z_ndc = -projection[2][2] - projection[3][2] / z_view.
    return abs(v_Settings.y) / max(abs(depth + v_Settings.x), 0.00001);
}

vec3 ApplyDepthOfField(vec3 center, vec2 texel)
{
    float focusDistance = v_Controls.x;
    float depth = ViewDepth(v_UV);
    float radius = v_Controls.z * smoothstep(0.0, max(v_Controls.y, 0.0001), abs(depth - focusDistance) / max(depth, 0.0001));
    if (radius < 0.5)
        return center;

    vec3 color = center;
    float weight = 1.0;
    for (int i = 0; i < 12; ++i)
    {
        float angle = float(i) * 2.399963;
        float distanceFromCenter = sqrt((float(i) + 0.5) / 12.0);
        vec2 uv = v_UV + vec2(cos(angle), sin(angle)) * distanceFromCenter * radius * texel;
        // Reject nearer geometry when blurring a background surface.
        if (ViewDepth(uv) >= depth * 0.9)
        {
            color += texture(u_Input, uv).rgb;
            weight += 1.0;
        }
    }
    return color / weight;
}

vec3 ApplyMotionBlur(vec3 center, vec2 texel)
{
    vec2 centerVelocity = texture(u_Auxiliary, v_UV).xy * v_Controls.x;
    float speed = length(centerVelocity / texel);
    if (speed < 0.5)
        return center;

    vec2 velocity = centerVelocity * min(1.0, v_Controls.y / speed);
    vec3 color = center;
    float weight = 1.0;
    for (int i = 1; i <= 6; ++i)
    {
        vec2 uv = v_UV - velocity * (float(i) / 6.0);
        vec2 neighborVelocity = texture(u_Auxiliary, uv).xy * v_Controls.x;
        if (length((neighborVelocity - centerVelocity) / texel) < 2.0)
        {
            color += texture(u_Input, uv).rgb;
            weight += 1.0;
        }
    }
    return color / weight;
}

vec3 ApplyFXAA(vec3 center, vec2 texel)
{
    float nw = Luminance(clamp(texture(u_Input, v_UV + texel * vec2(-1, -1)).rgb, 0.0, 1.0));
    float ne = Luminance(clamp(texture(u_Input, v_UV + texel * vec2( 1, -1)).rgb, 0.0, 1.0));
    float sw = Luminance(clamp(texture(u_Input, v_UV + texel * vec2(-1,  1)).rgb, 0.0, 1.0));
    float se = Luminance(clamp(texture(u_Input, v_UV + texel * vec2( 1,  1)).rgb, 0.0, 1.0));
    float middle = Luminance(clamp(center, 0.0, 1.0));
    float low = min(middle, min(min(nw, ne), min(sw, se)));
    float high = max(middle, max(max(nw, ne), max(sw, se)));
    if (high - low < max(v_Controls.y, high * v_Controls.x))
        return center;

    vec2 direction = vec2(sw + se - nw - ne, nw + sw - ne - se);
    float bias = max((nw + ne + sw + se) / 32.0, 1.0 / 128.0);
    direction = clamp(direction / (min(abs(direction.x), abs(direction.y)) + bias), -v_Controls.z, v_Controls.z);
    vec2 span = direction * texel;
    vec3 inner = 0.5 * (texture(u_Input, v_UV - span / 6.0).rgb +
                        texture(u_Input, v_UV + span / 6.0).rgb);
    vec3 outer = 0.5 * inner + 0.25 * (texture(u_Input, v_UV - span * 0.5).rgb +
                                      texture(u_Input, v_UV + span * 0.5).rgb);
    float outerLuma = Luminance(clamp(outer, 0.0, 1.0));
    return outerLuma < low || outerLuma > high ? inner : outer;
}

void main()
{
    int effect = int(v_Settings.z);
    vec2 texel = 1.0 / vec2(textureSize(u_Input, 0));

    if (effect == Prefilter)
    {
        // Extract before averaging so isolated HDR highlights survive downsampling.
        vec2 offset = texel * 0.5;
        vec3 highlights = ExtractHighlights(texture(u_Input, v_UV + vec2(-offset.x, -offset.y)).rgb)
                        + ExtractHighlights(texture(u_Input, v_UV + vec2( offset.x, -offset.y)).rgb)
                        + ExtractHighlights(texture(u_Input, v_UV + vec2(-offset.x,  offset.y)).rgb)
                        + ExtractHighlights(texture(u_Input, v_UV + vec2( offset.x,  offset.y)).rgb);
        o_Color = vec4(highlights * 0.25, 1.0);
        return;
    }
    if (effect == Gaussian)
    {
        o_Color = GaussianBlur(v_UV, texel * v_Settings.xy);
        return;
    }

    vec4 scene = texture(u_Input, v_UV);
    vec3 color = scene.rgb;
    switch (effect)
    {
    case Composite:
        color += texture(u_Auxiliary, v_UV).rgb * v_Controls.x;
        break;
    case DepthOfField:
        color = ApplyDepthOfField(color, texel);
        break;
    case MotionBlur:
        color = ApplyMotionBlur(color, texel);
        break;
    case ToneMapping:
        // Compress scene-linear HDR and encode it for the viewport's UNORM output.
        color = max(color, vec3(0.0)) * exp2(v_Controls.x);
        color = color / (1.0 + color);
        color = pow(color, vec3(1.0 / max(v_Controls.y, 0.01)));
        break;
    case ColorGrading:
        color = mix(vec3(Luminance(color)), color, v_Controls.w) * v_Controls.rgb;
        color = max(color, vec3(0.0));
        break;
    case Sharpen:
    {
        vec3 neighbors = texture(u_Input, v_UV + vec2(texel.x, 0)).rgb
                       + texture(u_Input, v_UV - vec2(texel.x, 0)).rgb
                       + texture(u_Input, v_UV + vec2(0, texel.y)).rgb
                       + texture(u_Input, v_UV - vec2(0, texel.y)).rgb;
        color = max(color + v_Controls.x * (color - neighbors * 0.25), vec3(0.0));
        break;
    }
    case ChromaticAberration:
    {
        vec2 radial = (v_UV - 0.5) * 2.0;
        vec2 offset = radial * dot(radial, radial) * texel * v_Controls.x;
        color.r = texture(u_Input, v_UV + offset).r;
        color.b = texture(u_Input, v_UV - offset).b;
        break;
    }
    case Vignette:
        color *= 1.0 - v_Controls.x * smoothstep(v_Controls.y, max(v_Controls.z, v_Controls.y + 0.0001), length((v_UV - 0.5) * 2.0));
        break;
    case FXAA:
        color = ApplyFXAA(color, texel);
        break;
    }
    o_Color = vec4(color, scene.a);
}
