#shader vertex
#version 430 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 v_TexCoord;

void main()
{
    v_TexCoord = a_TexCoord;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

#shader fragment
#version 430 core

layout(location = 0) in vec2 v_TexCoord;

layout(location = 0) out vec4 o_Color;

layout(binding = 10) uniform sampler2D u_GBufferNormal;
layout(binding = 11) uniform sampler2D u_GBufferAlbedo;
layout(binding = 12) uniform sampler2D u_GBufferSpecular;
layout(binding = 13) uniform sampler2D u_Depth;
layout(binding = 14) uniform samplerCube u_Skybox;



layout(std140, binding = 0) uniform PerPass_DeferredLighting
{
    mat4  u_InvViewProjNoTrans;     
    mat4  u_View;               
    vec4  u_ViewPos;                
    vec2  u_ViewportSize;           
    vec2  u_ShadowMapSize;       
    vec4  u_LightDir;               // offset 192 (vec3 → vec4)
    vec4  u_LightColor;             // offset 208 (vec3 → vec4)
    float u_Near;                   // offset 260
    float u_Far;                    // offset 264   
};
struct Light
{
    vec4 positionRadius;
    vec4 directionType;
    vec4 colorIntensity;
    vec4 params;
};
layout(std430, binding = 10) readonly buffer LightBuffer
{
    Light lights[];
};


void main(){
    float depth = texture(u_Depth, v_TexCoord).r;

    vec4 clipPos = vec4(v_TexCoord * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = u_InvViewProjNoTrans * clipPos;
    worldPos /= worldPos.w;
    // if (depth >= 1.0)
    // {
    //     vec3 viewDir = normalize(worldPos.xyz);
    //     o_Color = texture(u_Skybox, viewDir);
    //     return;
    // }

    vec3 normal     = normalize(texture(u_GBufferNormal, v_TexCoord).rgb);
    vec4 albedo     = texture(u_GBufferAlbedo, v_TexCoord);
    vec4 specData   = texture(u_GBufferSpecular, v_TexCoord);

    vec3 lightDirection = normalize(-u_LightDir.xyz);
    vec3 viewDirection  = normalize(u_ViewPos - worldPos).xyz;


    vec3 diffuseColor   = albedo.rgb;
    vec3 specularColor  = specData.rgb;
    float shininess     = specData.a * 255.0;


    float diff = max(dot(normal, lightDirection), 0.0);
    diff = diff * 0.5 + 0.5;
    vec3 diffuse = diff * u_LightColor.xyz * diffuseColor;

    // Specular (Blinn-Phong, matches Lit.shader)
    vec3 halfwayDir = normalize(lightDirection + viewDirection);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    vec3 specular = spec * u_LightColor.xyz * specularColor;
    o_Color = vec4(diffuse + specular, 1.0);
}