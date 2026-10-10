#shader vertex
#version 420 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

// Same block the GL push-constant path writes: pc.model and pc.prevModel.
// prevModel's first column is the mesh tint (rgb) and a selected flag (a).
layout(push_constant) uniform DrawPush
{
    mat4 model;
    mat4 prevModel;
} pc;

layout(std140, binding = 0) uniform PreviewCamera
{
    mat4 viewProj;
};

layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec3 v_Tint;
layout(location = 2) out float v_Selected;

void main()
{
    gl_Position = viewProj * pc.model * vec4(aPos, 1.0);
    v_Normal = mat3(pc.model) * aNormal;
    v_Tint = pc.prevModel[0].rgb;
    v_Selected = pc.prevModel[0].a;
}

#shader fragment
#version 420 core

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 v_Normal;
layout(location = 1) in vec3 v_Tint;
layout(location = 2) in float v_Selected;

void main()
{
    vec3 n = v_Normal;
    float len2 = dot(n, n);
    n = len2 > 1e-8 ? normalize(n) : vec3(0.0, 1.0, 0.0);

    vec3 key = normalize(vec3(0.35, 0.85, 0.4));
    vec3 fill = normalize(vec3(-0.45, 0.25, -0.55));
    float ndotl = max(dot(n, key), 0.0);
    float fillTerm = max(dot(n, fill), 0.0);
    vec3 color = v_Tint * (0.22 + 0.78 * ndotl + 0.18 * fillTerm);
    if (v_Selected > 0.5)
        color += vec3(0.06);
    FragColor = vec4(color, 1.0);
}
