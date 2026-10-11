#shader vertex
#version 420 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

// Same block the GL push-constant path writes: pc.model and pc.prevModel.
// prevModel column 0: mesh color (rgb) and selected flag (a).
// prevModel column 1 x: view mode. 0 shaded, 1 wireframe, 2 normals, 3 UV.
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
layout(location = 1) out vec3 v_Color;
layout(location = 2) out vec2 v_UV;
layout(location = 3) out float v_Selected;
layout(location = 4) out float v_Mode;

void main()
{
    gl_Position = viewProj * pc.model * vec4(aPos, 1.0);
    v_Normal = mat3(pc.model) * aNormal;
    v_Color = pc.prevModel[0].rgb;
    v_Selected = pc.prevModel[0].a;
    v_UV = aUV;
    v_Mode = pc.prevModel[1].x;
}

#shader fragment
#version 420 core

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 v_Normal;
layout(location = 1) in vec3 v_Color;
layout(location = 2) in vec2 v_UV;
layout(location = 3) in float v_Selected;
layout(location = 4) in float v_Mode;

void main()
{
    int mode = int(v_Mode + 0.5);
    vec3 n = v_Normal;
    float len2 = dot(n, n);
    n = len2 > 1e-8 ? normalize(n) : vec3(0.0, 1.0, 0.0);

    vec3 color;
    if (mode == 2)
    {
        color = n * 0.5 + 0.5;
    }
    else if (mode == 3)
    {
        color = vec3(fract(v_UV), 0.0);
    }
    else if (mode == 1)
    {
        color = vec3(0.62, 0.63, 0.66);
    }
    else
    {
        vec3 key = normalize(vec3(0.35, 0.85, 0.4));
        vec3 fill = normalize(vec3(-0.45, 0.25, -0.55));
        float ndotl = max(dot(n, key), 0.0);
        float fillTerm = max(dot(n, fill), 0.0);
        color = v_Color * (0.22 + 0.78 * ndotl + 0.18 * fillTerm);
    }

    if (v_Selected > 0.5 && mode == 0)
        color += vec3(0.06);
    FragColor = vec4(color, 1.0);
}
