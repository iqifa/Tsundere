#render_pass forward
#shader vertex
#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

layout(push_constant) uniform DrawPush
{
    mat4 model;
    mat4 prevModel;
} pc;

layout(std140, binding = 2) uniform PerFrame_Camera
{
    mat4 jitteredViewProj;
    mat4 viewProj;
    mat4 prevViewProj;
};

layout(location = 0) out vec2 v_TexCoords;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec4 v_CurrentClipPos;
layout(location = 3) out vec4 v_PreviousClipPos;
layout(location = 4) out vec3 v_WorldPos;

void main()
{
    vec4 worldPos = pc.model * vec4(aPos, 1.0);
    v_WorldPos = worldPos.xyz;
    gl_Position = jitteredViewProj * worldPos;
    v_TexCoords = vec2(aTexCoords.x, 1.0 - aTexCoords.y);
    v_Normal = normalize(mat3(transpose(inverse(pc.model))) * aNormal);
    v_CurrentClipPos = viewProj * worldPos;
    v_PreviousClipPos = prevViewProj * pc.prevModel * vec4(aPos, 1.0);
}

#shader fragment
#version 430 core

layout(location = 0) in vec2 v_TexCoords;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec4 v_CurrentClipPos;
layout(location = 3) in vec4 v_PreviousClipPos;
layout(location = 4) in vec3 v_WorldPos;

#include "include/Lighting.glsl"
layout(std430, binding = 4) readonly buffer ForwardLocalLights
{
    Light localLights[];
};

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec2 MotionVector;

layout(std140, binding = 1) uniform PerFrame_Geometry
{
    vec4 lightDir;
    vec4 lightColor;
    float ambientStrength;
    float _pad0;
    float _pad1;
    float _pad2;
    vec4 viewPos;
    mat4 u_LightViewProj;
    int hasNormalMap;
    int u_ShadowMapEnabled;
    vec2 u_ShadowMapSize;
    float u_LightSize;
    int _pad3;
    int _pad4;
    int _pad5;
};

layout(std140, binding = 3) uniform MaterialParams
{
    vec4 u_BaseColor;
};

void main()
{
    vec3 N = dot(v_Normal, v_Normal) > 1e-8 ? normalize(v_Normal) : vec3(0.0, 1.0, 0.0);
    // The forward pass supplies the active scene directional light. Its color
    // already includes intensity and is zero when no directional light is active.
    float directionLength = length(lightDir.xyz);
    vec3 L = directionLength > 1e-8 ? -lightDir.xyz / directionLength : vec3(0.0);
    float diffuse = max(dot(N, L), 0.0);
    vec3 color = u_BaseColor.rgb * lightColor.rgb * (max(ambientStrength, 0.0) + diffuse);
    for (int i = 0; i < localLights.length(); ++i)
    {
        Light light = localLights[i];
        if (light.colorIntensity.a <= 0.0 || light.params.x <= 0.0) continue;
        if (LightType(light) == LIGHT_AREA)
        {
            if (dot(light.directionAmbient.xyz, light.directionAmbient.xyz) <= 1e-16) continue;
            if (int(light.params.z + 0.5) != AREA_SHAPE_DISK && light.params.y <= 0.0) continue;
            Surface surface;
            surface.P = v_WorldPos;
            surface.N = N;
            vec3 toView = viewPos.xyz - v_WorldPos;
            surface.V = dot(toView, toView) > 1e-8 ? normalize(toView) : N;
            surface.diffuseColor = u_BaseColor.rgb;
            surface.specularColor = vec3(0.0);
            surface.shininess = 1.0;
            LightSample areaSample;
            if (SampleArea(light, surface, areaSample))
                color += u_BaseColor.rgb * areaSample.radiance * areaSample.diffuseTerm;
            continue;
        }
        vec3 toLight = light.positionType.xyz - v_WorldPos;
        float distanceToLight = length(toLight);
        vec3 localL = toLight / max(distanceToLight, 1e-4);
        float attenuation = pow(DistanceAttenuation(distanceToLight, light.params.x), max(light.params.y, 0.1));
        if (LightType(light) == LIGHT_SPOT)
        {
            float lengthDirection = length(light.directionAmbient.xyz);
            if (lengthDirection <= 1e-8) continue;
            float inner = clamp(light.params.z, 0.0, 89.0);
            float outer = clamp(light.params.w, inner, 89.0);
            float cosInner = cos(radians(inner));
            float cosOuter = cos(radians(outer));
            float theta = dot(-localL, light.directionAmbient.xyz / lengthDirection);
            // Equal angles define a hard edge; smoothstep would be undefined.
            attenuation *= cosInner - cosOuter > 1e-6
                ? smoothstep(cosOuter, cosInner, theta) : step(cosOuter, theta);
        }
        color += u_BaseColor.rgb * LightRadiance(light) * attenuation * max(dot(N, localL), 0.0);
    }
    FragColor = vec4(color, 1.0);

    vec2 currentNDC = v_CurrentClipPos.xy / v_CurrentClipPos.w;
    vec2 previousNDC = v_PreviousClipPos.xy / v_PreviousClipPos.w;
    MotionVector = (currentNDC - previousNDC) * 0.5;
}
