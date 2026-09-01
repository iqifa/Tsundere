#shader vertex

#version 420 core



layout(location = 0) in vec2 a_Position;

layout(location = 1) in vec2 a_TexCoord;



out vec2 v_TexCoord;
void main()
{

    v_TexCoord = a_TexCoord;

    gl_Position = vec4(a_Position, 0.0, 1.0);

}

#shader fragment

#version 420 core



in vec2 v_TexCoord;

layout(location = 0) out vec4 o_Color;



// --- bindings (descriptor set 0) ---
// Textures: bindings 10..14
// PerPass UBO: binding 0

layout(binding = 10) uniform sampler2D u_CurrentColor;   // 0: 当前帧颜色 (几何 pass)
layout(binding = 11) uniform sampler2D u_HistoryColor;   // 1: 上一帧历史颜色
layout(binding = 12) uniform sampler2D u_VelocityTex;    // 2: 动态模糊速度图
layout(binding = 13) uniform sampler2D u_DepthTex;       // 3: 深度图
layout(binding = 14) uniform sampler2D u_HistoryDepthTex;// 4: 上一帧的历史深度图

layout(std140, binding = 0) uniform PerPass_TAA {
    mat4 u_InverseViewProj;   // offset  0,  64
    mat4 u_PrevViewProj;      // offset 64,  64
};



// --- ���Ŀ���Ӱ���� 1���ٶ����� (Velocity Dilation) ---

vec2 GetClosestVelocity(vec2 uv)

{

    vec2 texelSize = 1.0 / vec2(textureSize(u_DepthTex, 0));

    float closestDepth = 1.0;

    vec2 closestUV = uv;



    for(int y = -1; y <= 1; ++y) {

        for(int x = -1; x <= 1; ++x) {

            vec2 offsetUV = uv + vec2(x, y) * texelSize;

            float depth = texture(u_DepthTex, offsetUV).r;

            if(depth < closestDepth) {

                closestDepth = depth;

                closestUV = offsetUV;

            }

        }

    }

    return texture(u_VelocityTex, closestUV).rg;

}



// --- ���Ŀ���Ӱ���� 2��Playdead �� Clip AABB ---

vec3 ClipAABB(vec3 aabbMin, vec3 aabbMax, vec3 current, vec3 history)

{

    vec3 center = 0.5 * (aabbMax + aabbMin);

    vec3 extents = 0.5 * (aabbMax - aabbMin) + vec3(0.00001);



    vec3 rayDir = history - center;

    vec3 rayUnit = rayDir / extents;

    vec3 absUnit = abs(rayUnit);

    float maxUnit = max(absUnit.x, max(absUnit.y, absUnit.z));



    if (maxUnit > 1.0)

        return center + rayDir / maxUnit;

    else

        return history;

}



void main()

{

    vec3 currentColor = texture(u_CurrentColor, v_TexCoord).rgb;

    float currentDepth = texture(u_DepthTex, v_TexCoord).r;



    // ���޸ĵ㡿��ɾ���˴������պ� early-out ���У����������ع�ƽ������ͶӰ��



    // ==========================================

    // 1. ��ȡ�ٶ� (ʹ���ٶ����ţ�)

    // ==========================================

    vec2 velocity = GetClosestVelocity(v_TexCoord);



    // ��̬��������պм�������ƶ��ٶ�

    if (length(velocity) < 0.00001)

    {

        // ��ʹ depth �� 1.0 (��պ�)����������Ҳ����ȷ�����պе���תƽ��ƫ�ƣ�

        vec4 ndcPos = vec4(v_TexCoord * 2.0 - 1.0, currentDepth * 2.0 - 1.0, 1.0);

        vec4 worldPos = u_InverseViewProj * ndcPos;

        worldPos /= worldPos.w;

        vec4 prevClipPos = u_PrevViewProj * worldPos;

        vec2 prevNDC = prevClipPos.xy / prevClipPos.w;

        vec2 prevUV = prevNDC * 0.5 + 0.5;

       

        velocity = v_TexCoord - prevUV;

    }

   

    vec2 historyUV = v_TexCoord - velocity;



    // ��Ļ�߽���

    if (historyUV.x < 0.0 || historyUV.x > 1.0 || historyUV.y < 0.0 || historyUV.y > 1.0)

    {

        o_Color = vec4(currentColor, 1.0);

        return;

    }



    // ==========================================

    // 2. ��ȶԱ��޳� (Depth Rejection)

    // ==========================================

    float historyDepth = texture(u_HistoryDepthTex, historyUV).r;

   

    // �����պ�©�����ˣ����� historyDepth �ͻ��Ǿ����ӵ���ȣ��Ӷ������޳����ɵ���Ӱ��

    // if (abs(currentDepth - historyDepth) > 0.05)

    // {

    //     o_Color = vec4(currentColor, 1.0);

    //     return;

    // }



    // ==========================================

    // 3. ��ɫ�����ȡ�� Clip AABB ǯ��

    // ==========================================

    vec3 historyColor = texture(u_HistoryColor, historyUV).rgb;



    vec2 texelSize = 1.0 / vec2(textureSize(u_CurrentColor, 0));

    vec3 minColor = currentColor;

    vec3 maxColor = currentColor;

   

    for (int x = -1; x <= 1; ++x) {

        for (int y = -1; y <= 1; ++y) {

            if (x == 0 && y == 0) continue;

            vec3 neighborColor = texture(u_CurrentColor, v_TexCoord + vec2(x, y) * texelSize).rgb;

            minColor = min(minColor, neighborColor);

            maxColor = max(maxColor, neighborColor);

        }

    }

   

    historyColor = ClipAABB(minColor, maxColor, currentColor, historyColor);



    // ==========================================

    // 4. ���ջ��

    // ==========================================

    float blendFactor = .05;

   

    vec3 finalColor = mix(historyColor, currentColor, blendFactor);

    o_Color = vec4(finalColor, 1.0);

}