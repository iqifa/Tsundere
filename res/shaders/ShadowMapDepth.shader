#shader vertex
#version 330 core

layout(location = 0) in vec3 aPos;


layout(push_constant) uniform DrawPush
{
    mat4 u_Model;
}

layout(std140, binding = 0) uniform PerPass_ShadowMapDepth
{
    mat4 u_LightViewProj;
};


void main()
{
    gl_Position = u_LightViewProj * u_Model * vec4(aPos, 1.0);
}

#shader fragment
#version 330 core

void main()
{
    // Depth is written automatically by the fixed-function depth pipeline.
    // glDrawBuffer(GL_NONE) on the FBO discards any color output.
}
