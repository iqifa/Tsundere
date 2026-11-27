#shader vertex
#version 430 core

layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform LightVolumePush
{
    mat4 u_ModelViewProj;
    int  u_LightIndex;
    int  _pad0;
    int  _pad1;
    int  _pad2;
} pc;

void main()
{
    gl_Position = pc.u_ModelViewProj * vec4(a_Position, 1.0);
}

#shader fragment
#version 430 core

// Color writes are disabled for the Depth-Fail Stencil volume marking pass.
void main()
{
}
