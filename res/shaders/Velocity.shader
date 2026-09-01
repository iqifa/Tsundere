#shader vertex
#version 330 core
layout(location = 0) in vec3 a_Position;

// 动态物体需要当前帧和上一帧的模型矩阵
uniform mat4 model;
uniform mat4 prevModel;

// 相机矩阵（切记：必须是【未加抖动】的纯净矩阵！）
    uniform mat4 viewProj;
uniform mat4 prevViewProj;

out vec4 v_CurrentClipPos;
out vec4 v_PreviousClipPos;

void main()
{
    // 计算当前帧的裁剪空间坐标
    v_CurrentClipPos = viewProj * model * vec4(a_Position, 1.0);
    // 计算上一帧的裁剪空间坐标
    v_PreviousClipPos = prevViewProj * prevModel * vec4(a_Position, 1.0);
    
    gl_Position = v_CurrentClipPos;
}


#shader fragment
#version 330 core

in vec4 v_CurrentClipPos;
in vec4 v_PreviousClipPos;

// 输出到 RG16F 纹理，表示 UV 空间的二维速度 (X, Y)
layout(location = 0) out vec2 o_Velocity;

void main()
{
    // 1. 透视除法，从 Clip Space 转换到 NDC [-1, 1]
    vec2 currentNDC = v_CurrentClipPos.xy / v_CurrentClipPos.w;
    vec2 previousNDC = v_PreviousClipPos.xy / v_PreviousClipPos.w;
    
    // 2. 将 NDC 映射到 UV 空间 [0, 1]
    vec2 currentUV = currentNDC * 0.5 + 0.5;
    vec2 previousUV = previousNDC * 0.5 + 0.5;
    
    // 3. 计算速度：当前 UV 减去 历史 UV
    // 这样在 TAA 中：历史 UV = 当前 UV - 速度
    o_Velocity = currentUV - previousUV;
}