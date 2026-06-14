#shader vertex
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

[System]
uniform mat4 u_LightViewProj;
uniform mat4 u_Model;
[System]

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
