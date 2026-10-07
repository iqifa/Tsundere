#shader vertex
#version 420 core
layout (location = 0) in vec3 aPos;

layout(std140, binding = 0) uniform SkyBoxMatrices
{
    mat4 proj;
    mat4 view;
    mat4 prevViewProj;
};

layout(location = 0) out vec3 TexCoords;

void main()
{
    TexCoords = aPos;
    gl_Position = proj * view * vec4(aPos, 1.0);
}

#shader fragment
#version 420 core
layout(location = 0) out vec4 FragColor;

layout(binding = 10) uniform samplerCube skybox;

layout(location = 0) in vec3 TexCoords;

void main()
{
    FragColor = texture(skybox, TexCoords);
}
