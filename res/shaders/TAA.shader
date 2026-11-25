#shader vertex

#version 420 core



layout(location = 0) in vec2 a_Position;

layout(location = 1) in vec2 a_TexCoord;



layout(location = 0) out vec2 v_TexCoord;
void main()
{

    v_TexCoord = a_TexCoord;

    gl_Position = vec4(a_Position, 0.0, 1.0);

}

#shader fragment

#version 420 core



layout(location = 0) in vec2 v_TexCoord;

layout(location = 0) out vec4 o_Color;



// --- bindings (descriptor set 0) ---
// Textures: bindings 10..13
// PerPass UBO: binding 0

layout(binding = 10) uniform sampler2D u_CurrentColor;   // 0: 当前帧颜色 (几何 pass)
layout(binding = 11) uniform sampler2D u_HistoryColor;   // 1: 上一帧历史颜色
layout(binding = 12) uniform sampler2D u_VelocityTex;    // 2: 动态模糊速度图
layout(binding = 13) uniform sampler2D u_DepthTex;       // 3: 深度图

layout(std140, binding = 0) uniform PerPass_TAA {
    mat4 u_InverseViewProj;   // offset  0,  64
    mat4 u_PrevViewProj;      // offset 64,  64
    vec2 u_CurrentJitter;
    int u_HistoryValid;
    int _pad0;
};



// --- ���Ŀ���Ӱ���� 1���ٶ����� (Velocity Dilation) ---

ivec2 GetClosestSurfacePixel(ivec2 pixel, ivec2 size, out float closestDepth)
{
    closestDepth = texelFetch(u_DepthTex, pixel, 0).r;
    ivec2 closestPixel = pixel;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
                continue;
            ivec2 neighbor = clamp(pixel + ivec2(x, y), ivec2(0), size - 1);
            float depth = texelFetch(u_DepthTex, neighbor, 0).r;
            if (depth < closestDepth)
            {
                closestDepth = depth;
                closestPixel = neighbor;
            }
        }
    }
    return closestPixel;
}



// --- ���Ŀ���Ӱ���� 2��Playdead �� Clip AABB ---

vec4 ClipAABB(vec4 aabbMin, vec4 aabbMax, vec4 current, vec4 history)

{

    vec4 center = 0.5 * (aabbMax + aabbMin);

    vec4 extents = 0.5 * (aabbMax - aabbMin) + vec4(0.00001);



    vec4 rayDir = history - center;

    vec4 rayUnit = rayDir / extents;

    vec4 absUnit = abs(rayUnit);

    float maxUnit = max(max(absUnit.x, absUnit.y), max(absUnit.z, absUnit.w));



    if (maxUnit > 1.0)

        return center + rayDir / maxUnit;

    else

        return history;

}



void main()

{

    // A clear center can still touch a silhouette after MSAA depth resolve.
    // Only bypass TAA when the entire neighborhood is clear.
    ivec2 size = textureSize(u_DepthTex, 0);
    vec2 currentUV = v_TexCoord + u_CurrentJitter;
    ivec2 pixel = clamp(ivec2(currentUV * vec2(size)), ivec2(0), size - 1);
    // Point-sample depth; interpolating across silhouettes invents surfaces.
    gl_FragDepth = texelFetch(u_DepthTex, pixel, 0).r;
    float closestDepth;
    ivec2 closestPixel = GetClosestSurfacePixel(pixel, size, closestDepth);
    if (closestDepth >= 1.0)
    {
        o_Color = texture(u_CurrentColor, currentUV);
        return;
    }

    // Reconstruct the jittered input on the stable grid used by the history.
    // Alpha is opaque coverage, not display opacity. Filter and clip it with RGB
    // so the later stable sky pass can fill subpixel silhouette coverage.
    vec4 currentColor = texture(u_CurrentColor, currentUV);
    if (u_HistoryValid == 0)
    {
        o_Color = currentColor;
        return;
    }

    vec2 velocity = texelFetch(u_VelocityTex, closestPixel, 0).rg;
    if (length(velocity) < 0.00001)
    {
        // Reproject the same surface used for velocity dilation.
        float ndcDepth = closestDepth;
#ifndef TS_VULKAN
        ndcDepth = ndcDepth * 2.0 - 1.0;
#endif
        vec2 closestUV = (vec2(closestPixel) + 0.5) / vec2(size);
        vec4 ndcPos = vec4(closestUV * 2.0 - 1.0, ndcDepth, 1.0);

        vec4 worldPos = u_InverseViewProj * ndcPos;

        worldPos /= worldPos.w;

        vec4 prevClipPos = u_PrevViewProj * worldPos;

        vec2 prevNDC = prevClipPos.xy / prevClipPos.w;

        vec2 prevUV = prevNDC * 0.5 + 0.5;

       

        velocity = closestUV - u_CurrentJitter - prevUV;

    }

   

    vec2 historyUV = v_TexCoord - velocity;



    // ��Ļ�߽���

    if (historyUV.x < 0.0 || historyUV.x > 1.0 || historyUV.y < 0.0 || historyUV.y > 1.0)

    {

        o_Color = currentColor;

        return;

    }



    // ==========================================

    // 2. ��ɫ�����ȡ�� Clip AABB ǯ��

    // ==========================================

    vec4 historyColor = texture(u_HistoryColor, historyUV);



    vec2 texelSize = 1.0 / vec2(textureSize(u_CurrentColor, 0));

    vec4 minColor = currentColor;

    vec4 maxColor = currentColor;

   

    for (int x = -1; x <= 1; ++x) {

        for (int y = -1; y <= 1; ++y) {

            if (x == 0 && y == 0) continue;

            vec4 neighborColor = texture(u_CurrentColor, currentUV + vec2(x, y) * texelSize);

            minColor = min(minColor, neighborColor);

            maxColor = max(maxColor, neighborColor);

        }

    }

   

    historyColor = ClipAABB(minColor, maxColor, currentColor, historyColor);



    // ==========================================

    // 3. ���ջ��

    // ==========================================

    float blendFactor = .05;

   

    vec4 finalColor = mix(historyColor, currentColor, blendFactor);

    o_Color = finalColor;

}
