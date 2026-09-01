#shader vertex
#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

[System]
uniform mat4 MVP_matrix;
uniform mat4 model;
[System]
out vec2 TexCoords;
out vec3 Normal;

void main()
{
    gl_Position = MVP_matrix * vec4(aPos, 1.0);
    Normal=(model*vec4(aNormal,1.0)).rgb;
    TexCoords=aTexCoords;
    TexCoords.y=1-TexCoords.y;
}
#shader fragment
#version 430 core

layout(location = 0) out vec4 FragColor;
[System]
uniform vec3 lightDir;
uniform vec3 lightColor;
[System]
in vec3 Normal;
in vec2 TexCoords;
uniform sampler2D texture_diffuse1;

void main()
{
    

    // Light direction
    vec3 L = normalize(-lightDir);

    // Lambert
    float NdotL = max(dot(normalize(Normal), L), 0.0);


    // vec3 albedo=texture(texture_diffuse1,TexCoords).rgb;
    vec3 albedo=vec3(1.0,1.0,1.0);
    vec3 diffuse = albedo*lightColor * NdotL;

    FragColor = vec4(diffuse, 1.0);
}
