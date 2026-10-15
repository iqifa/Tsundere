#shader vertex
#version 450

layout(location = 0) in vec3 aPos;

layout(push_constant) uniform DrawPush
{
    mat4 model;
    mat4 prevModel;
} pc;

layout(std140, binding = 0) uniform PerPass_ShadowMapDepth
{
    mat4 u_LightViewProj;
};

void main()
{
    gl_Position = u_LightViewProj * pc.model * vec4(aPos, 1.0);
}

#shader fragment
#version 450

layout(location = 0) out vec4 o_Vis;

void main()
{
    // Window depth is 0 at the light's near plane and 1 at the far plane.
    // Replicate it so the debug view is gray instead of a one-channel red image.
    float depth = gl_FragCoord.z;
    o_Vis = vec4(depth, depth, depth, 1.0);
}
