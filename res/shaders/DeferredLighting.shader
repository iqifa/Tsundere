#shader vertex
#version 430 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;

out vec2 v_TexCoord;

void main()
{
    v_TexCoord = a_TexCoord;
    gl_Position = vec4(a_Position, 0.0, 1.0);
}

#shader fragment
#version 430 core

in vec2 v_TexCoord;
layout(location = 0) out vec4 o_Color;

// Samplers (descriptor set 0)
layout(binding = 10) uniform sampler2D u_GBufferPosition;
layout(binding = 11) uniform sampler2D u_GBufferNormal;
layout(binding = 12) uniform sampler2D u_GBufferAlbedo;
layout(binding = 13) uniform sampler2D u_GBufferSpecular;
layout(binding = 14) uniform sampler2D u_ShadowMask;
layout(binding = 15) uniform sampler2D u_Depth;
layout(binding = 16) uniform samplerCube u_Skybox;

layout(binding = 17) uniform sampler2D u_DDGIIrradiance;
layout(binding = 18) uniform sampler2D u_DDGIDepth;
layout(binding = 19) uniform sampler2D u_ShadowMap;

layout(std140, binding = 0) uniform PerPass_DeferredLighting
{
    mat4  u_InvViewProjNoTrans;     // offset   0
    mat4  u_View;                   // offset  64
    mat4  u_LightViewProj;          // offset 128
    vec4  u_LightDir;               // offset 192 (vec3 → vec4)
    vec4  u_LightColor;             // offset 208 (vec3 → vec4)
    vec4  u_ViewPos;                // offset 224 (vec3 → vec4)
    vec2  u_ViewportSize;           // offset 240
    vec2  u_ShadowMapSize;          // offset 248
    float u_AmbientStrength;        // offset 256
    float u_Near;                   // offset 260
    float u_Far;                    // offset 264
    float u_LightSize;              // offset 268
    float u_DDGISpacing;            // offset 272
    float u_DDGIProbeRadius;        // offset 276
    float u_DDGIDepthSharpness;     // offset 280
    int   u_DDGIEnabled;            // offset 284
    int   u_DDGIProbesPerRow;       // offset 288
    int   u_TileSize;               // offset 292
    int   u_ClusterCountX;          // offset 296
    int   u_ClusterCountY;          // offset 300
    int   u_ClusterCountZ;          // offset 304
    int   u_MaxLightsPerCluster;    // offset 308
    int   u_LocalLightCount;        // offset 312
    int   u_ClusteredLightingEnabled;// offset 316
    int   u_DebugMode;              // offset 320
    int   u_ShadowMapEnabled;       // offset 324
    int   u_DDGIGridSizeX;          // offset 328
    int   u_DDGIGridSizeY;          // offset 332
    int   u_DDGIGridSizeZ;          // offset 336
    // pad 12B  → next vec4 needs 16-byte align at 352
    vec4  u_DDGIGridOrigin;         // offset 352 (vec3 → vec4)
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

layout(std430, binding = 11) readonly buffer ClusterMetaBuffer
{
    uvec4 clusters[];
};

layout(std430, binding = 12) readonly buffer ClusterLightIndexBuffer
{
    uint clusterLightIndices[];
};

// ================================================================
// Octahedral encode  (maps direction -> [0,1]^2)
// ================================================================
vec2 OctEncode(vec3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 oct = n.z >= 0.0 ? vec2(n.x, n.y)
                          : (1.0 - abs(vec2(n.y, n.x))) * vec2(sign(n.x), sign(n.y));
    return oct * 0.5 + 0.5;
}

// ================================================================
// DDGI probe sampling — trilinear interpolation with visibility
// ================================================================
vec3 sampleDDGIIrradiance(vec3 worldPos, vec3 normal)
{
    vec3 gridCoord = (worldPos - u_DDGIGridOrigin.xyz) / u_DDGISpacing - 0.5;
    ivec3 baseProbe = ivec3(floor(gridCoord));
    vec3  frac      = fract(gridCoord);

    vec3 irradiance = vec3(0.0);
    float totalWeight = 0.0;

    for (int dx = 0; dx <= 1; dx++)
    for (int dy = 0; dy <= 1; dy++)
    for (int dz = 0; dz <= 1; dz++)
    {
        ivec3 probeCoord = clamp(baseProbe + ivec3(dx, dy, dz),
            ivec3(0),
            ivec3(u_DDGIGridSizeX - 1, u_DDGIGridSizeY - 1, u_DDGIGridSizeZ - 1));

        int probeIndex = probeCoord.z * u_DDGIGridSizeX * u_DDGIGridSizeY
                       + probeCoord.y * u_DDGIGridSizeX
                       + probeCoord.x;

        vec3 probeWorldPos = u_DDGIGridOrigin.xyz + (vec3(probeCoord) + 0.5) * u_DDGISpacing;
        vec3 toPoint = worldPos - probeWorldPos;
        float dist = length(toPoint);
        vec3 toPointDir = toPoint / max(dist, 0.0001);

        vec2 octUV = OctEncode(toPointDir);

        int probeCol = probeIndex % u_DDGIProbesPerRow;
        int probeRow = probeIndex / u_DDGIProbesPerRow;

        float atlasW = float(u_DDGIProbesPerRow * 8);
        float depthW = float(u_DDGIProbesPerRow * 16);

        // 2x2 box filter: smooths single-texel noise causing probe-visible artifacts
        float octTexelX = octUV.x * 7.0;
        float octTexelY = octUV.y * 7.0;
        int tx = clamp(int(octTexelX), 0, 6);
        int ty = clamp(int(octTexelY), 0, 6);
        vec3 probeIrradiance = vec3(0.0);
        for (int sx = 0; sx <= 1; sx++)
        for (int sy = 0; sy <= 1; sy++)
        {
            vec2 uv = vec2(float(probeCol * 8 + tx + sx),
                           float(probeRow * 8 + ty + sy)) / vec2(atlasW);
            probeIrradiance += texture(u_DDGIIrradiance, uv).rgb;
        }
        probeIrradiance *= 0.25;

        // Depth: single sample (less sensitive to noise, used for visibility only)
        vec2 depthUV = vec2(float(probeCol * 16) + octUV.x * 15.0 + 0.5,
                            float(probeRow * 16) + octUV.y * 15.0 + 0.5) / vec2(depthW);
        float probeDepth = texture(u_DDGIDepth, depthUV).r;

        float depthWeight = 1.0;
        if (u_DDGIDepthSharpness > 0.0)
            depthWeight = clamp(exp(-u_DDGIDepthSharpness * max(dist - probeDepth, 0.0)), 0.0, 1.0);

        float triWeight = (1.0 - abs(float(dx) - frac.x))
                        * (1.0 - abs(float(dy) - frac.y))
                        * (1.0 - abs(float(dz) - frac.z));

        float distWeight = 1.0 - smoothstep(0.0, u_DDGIProbeRadius, dist);

        float finalWeight = max(triWeight * distWeight * depthWeight, 0.0);
        irradiance += probeIrradiance * finalWeight;
        totalWeight += finalWeight;
    }

    vec3 result = totalWeight > 0.001 ? irradiance / totalWeight : vec3(0.0);

    // Grid edge fade: smooth transition at boundaries (no hard cutoff)
    vec3 gridFrac = gridCoord + 0.5; // [0, GridSize]
    float edgeFade = 1.0;
    edgeFade *= smoothstep(0.0, 1.5, gridFrac.x);
    edgeFade *= smoothstep(0.0, 1.5, gridFrac.y);
    edgeFade *= smoothstep(0.0, 1.5, gridFrac.z);
    edgeFade *= smoothstep(0.0, 1.5, float(u_DDGIGridSizeX) - gridFrac.x);
    edgeFade *= smoothstep(0.0, 1.5, float(u_DDGIGridSizeY) - gridFrac.y);
    edgeFade *= smoothstep(0.0, 1.5, float(u_DDGIGridSizeZ) - gridFrac.z);

    return result * edgeFade;
}
// ================================================================
// Interleaved gradient noise — cheap per-pixel random
// ================================================================
float interleavedGradientNoise(vec2 pos)
{
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pos, magic.xy)));
}

// 16-point Poisson disk (good for both blocker search and PCF)
const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.094184101,-0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
);

// ================================================================
// PCSS — Percentage Closer Soft Shadows
// ================================================================
float sampleShadowMap(vec3 worldPos, vec3 normal, vec3 lightDir)
{
    if (u_ShadowMapEnabled == 0)
        return 1.0;

    // Normal offset & backface cull
    float NdotL = max(dot(normal, lightDir), 0.0);
    if (NdotL <= 0.0)
        return 0.0;
    float normalOffset = 0.02 * (1.0 - NdotL) + 0.005;
    vec3 biasedPos = worldPos + normal * normalOffset;

    // Transform to light clip space
    vec4 lightClip = u_LightViewProj * vec4(biasedPos, 1.0);
    if (lightClip.w <= 0.0)
        return 1.0;

    vec3 lightNDC = lightClip.xyz / lightClip.w;
    vec2 uv = lightNDC.xy * 0.5 + 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0;

    float bias = max(0.005 * (1.0 - NdotL), 0.001);
    float fragDepth = lightNDC.z * 0.5 + 0.5 - bias;

    vec2 texelSize = 1.0 / u_ShadowMapSize;

    // ---- 1. Blocker Search ----
    const int  BLOCKER_SAMPLES = 16;
    const float BLOCKER_SEARCH_UV = 0.015; // ~30 texels search radius

    float avgBlockerDepth = 0.0;
    int   blockerCount    = 0;

    float n1 = interleavedGradientNoise(gl_FragCoord.xy);
    float a1 = n1 * 6.2831853;
    mat2  r1 = mat2(cos(a1), -sin(a1), sin(a1), cos(a1));

    for (int i = 0; i < BLOCKER_SAMPLES; i++)
    {
        vec2  offset = r1 * poissonDisk[i] * BLOCKER_SEARCH_UV;
        float depth  = texture(u_ShadowMap, uv + offset).r;
        if (fragDepth > depth)
        {
            avgBlockerDepth += depth;
            blockerCount++;
        }
    }

    // No blockers → fully lit
    if (blockerCount == 0)
        return 1.0;

    avgBlockerDepth /= float(blockerCount);

    // ---- 2. Penumbra Estimation ----
    // penumbraWidth = (receiver - blocker) / blocker × lightSize
    float penumbraRatio  = (fragDepth - avgBlockerDepth) / max(avgBlockerDepth, 0.0001);
    float filterTexels   = penumbraRatio * u_LightSize;
    filterTexels         = clamp(filterTexels, 1.0, 80.0);
    float filterRadiusUV = filterTexels / u_ShadowMapSize.x;

    // ---- 3. Adaptive PCF ----
    const int PCF_SAMPLES = 16;
    float shadow = 0.0;

    float n2 = interleavedGradientNoise(gl_FragCoord.xy + vec2(0.5, 0.5));
    float a2 = n2 * 6.2831853;
    mat2  r2 = mat2(cos(a2), -sin(a2), sin(a2), cos(a2));

    for (int i = 0; i < PCF_SAMPLES; i++)
    {
        vec2  offset = r2 * poissonDisk[i] * filterRadiusUV;
        float depth  = texture(u_ShadowMap, uv + offset).r;
        shadow += fragDepth <= depth ? 1.0 : 0.0;
    }
    shadow /= float(PCF_SAMPLES);

    return shadow;
}

int getClusterID(vec3 worldPos)
{
    int clusterX = int(gl_FragCoord.x) / max(u_TileSize, 1);
    int clusterY = int(gl_FragCoord.y) / max(u_TileSize, 1);

    vec3 viewPos = (u_View * vec4(worldPos, 1.0)).xyz;
    float viewDepth = max(-viewPos.z, u_Near);
    float z01 = clamp((viewDepth - u_Near) / max(u_Far - u_Near, 0.0001), 0.0, 0.9999);
    int clusterZ = int(z01 * float(max(u_ClusterCountZ, 1)));

    clusterX = clamp(clusterX, 0, max(u_ClusterCountX - 1, 0));
    clusterY = clamp(clusterY, 0, max(u_ClusterCountY - 1, 0));
    clusterZ = clamp(clusterZ, 0, max(u_ClusterCountZ - 1, 0));

    return clusterZ * u_ClusterCountX * u_ClusterCountY + clusterY * u_ClusterCountX + clusterX;
}

vec3 accumulatePointLight(uint lightIndex, vec3 worldPos, vec3 normal, vec3 viewDirection, vec3 diffuseColor, vec3 specularColor, float shininess)
{
    Light light = lights[lightIndex];
    vec3 lightPos = light.positionRadius.xyz;
    float radius = light.positionRadius.w;
    vec3 color = light.colorIntensity.rgb;
    float intensity = light.colorIntensity.w;
    float falloff = max(light.params.x, 0.1);

    vec3 toLight = lightPos - worldPos;
    float dist = length(toLight);
    if (dist >= radius || radius <= 0.0)
        return vec3(0.0);

    vec3 L = toLight / max(dist, 0.0001);
    float rangeAtten = clamp(1.0 - dist / radius, 0.0, 1.0);
    rangeAtten = pow(rangeAtten, falloff);
    float invSq = 1.0 / max(dist * dist, 1.0);
    float atten = rangeAtten * invSq * intensity;

    float NdotL = max(dot(normal, L), 0.0);
    vec3 diffuse = NdotL * color * diffuseColor;

    vec3 H = normalize(L + viewDirection);
    float spec = pow(max(dot(normal, H), 0.0), shininess);
    vec3 specular = spec * color * specularColor;

    return (diffuse + specular) * atten;
}

vec3 accumulateLocalLights(vec3 worldPos, vec3 normal, vec3 viewDirection, vec3 diffuseColor, vec3 specularColor, float shininess)
{
    vec3 localLighting = vec3(0.0);

    if (u_ClusteredLightingEnabled == 1 && u_ClusterCountX > 0 && u_ClusterCountY > 0 && u_ClusterCountZ > 0)
    {
        int clusterID = getClusterID(worldPos);
        uvec4 meta = clusters[clusterID];
        uint offset = meta.x;
        uint count = min(meta.y, uint(u_MaxLightsPerCluster));

        for (uint i = 0u; i < count; i++)
        {
            uint lightIndex = clusterLightIndices[offset + i];
            if (lightIndex < uint(u_LocalLightCount))
                localLighting += accumulatePointLight(lightIndex, worldPos, normal, viewDirection, diffuseColor, specularColor, shininess);
        }
    }
    else
    {
        for (int i = 0; i < u_LocalLightCount; i++)
            localLighting += accumulatePointLight(uint(i), worldPos, normal, viewDirection, diffuseColor, specularColor, shininess);
    }

    return localLighting;
}

void main()
{
    float depth = texture(u_Depth, v_TexCoord).r;

    // Sky pixel (no geometry rendered)
    if (depth >= 1.0)
    {
        // Reconstruct world-space direction from screen UV
        vec4 clipPos = vec4(v_TexCoord * 2.0 - 1.0, 0.99999, 1.0);
        vec4 worldPos = u_InvViewProjNoTrans * clipPos;
        worldPos /= worldPos.w;
        vec3 viewDir = normalize(worldPos.xyz);
        o_Color = texture(u_Skybox, viewDir);
        return;
    }

    // GBuffer reads
    vec3 worldPos   = texture(u_GBufferPosition, v_TexCoord).rgb;
    vec3 normal     = normalize(texture(u_GBufferNormal, v_TexCoord).rgb);
    vec4 albedo     = texture(u_GBufferAlbedo, v_TexCoord);
    vec4 specData   = texture(u_GBufferSpecular, v_TexCoord);
    float shadow    = texture(u_ShadowMask, v_TexCoord).r;

    // Debug mode: output raw GBuffer channels
    if (u_DebugMode == 1)  { o_Color = vec4(worldPos, 1.0); return; }
    if (u_DebugMode == 2)  { o_Color = vec4(normal * 0.5 + 0.5, 1.0); return; }
    if (u_DebugMode == 3)  { o_Color = albedo; return; }
    if (u_DebugMode == 4)  { o_Color = vec4(specData.rgb, 1.0); return; }
    // Linearize depth for display (non-linear z-buffer is unreadable)
    if (u_DebugMode == 5)
    {
        float z_ndc = depth * 2.0 - 1.0;
        float linearDepth = (2.0 * u_Near * u_Far) / (u_Far + u_Near - z_ndc * (u_Far - u_Near));
        float displayDepth = (linearDepth - u_Near) / (u_Far - u_Near);
        o_Color = vec4(vec3(displayDepth), 1.0); return;
    }

    // DDGI debug modes
    if (u_DebugMode == 6 && u_DDGIEnabled == 1)
    {
        o_Color = vec4(sampleDDGIIrradiance(worldPos, normal), 1.0);
        return;
    }
    if (u_DebugMode == 7 && u_DDGIEnabled == 1)
    {
        vec3 gridCoord = (worldPos - u_DDGIGridOrigin.xyz) / u_DDGISpacing - 0.5;
        ivec3 probeCoord = clamp(ivec3(floor(gridCoord + 0.5)),
            ivec3(0), ivec3(u_DDGIGridSizeX - 1, u_DDGIGridSizeY - 1, u_DDGIGridSizeZ - 1));
        vec3 probeWorldPos = u_DDGIGridOrigin.xyz + ((vec3(probeCoord) + 0.5) * u_DDGISpacing);
        vec3 toPointDir = normalize(worldPos - probeWorldPos);
        vec2 octUV = OctEncode(toPointDir);
        int pi = probeCoord.z * u_DDGIGridSizeX * u_DDGIGridSizeY
               + probeCoord.y * u_DDGIGridSizeX + probeCoord.x;
        int pc = pi % u_DDGIProbesPerRow, pr = pi / u_DDGIProbesPerRow;
        float d = texture(u_DDGIDepth,
            vec2(float(pc * 16) + octUV.x * 15.0 + 0.5,
                 float(pr * 16) + octUV.y * 15.0 + 0.5)
            / vec2(float(u_DDGIProbesPerRow * 16))).r;
        o_Color = vec4(vec3(d / (u_DDGIProbeRadius * 2.0)), 1.0);
        return;
    }

    // --- Blinn-Phong Lighting (matches Lit.shader) ---
    vec3 diffuseColor   = albedo.rgb;
    vec3 specularColor  = specData.rgb;
    float shininess     = specData.a;

    vec3 lightDirection = normalize(-u_LightDir.xyz);
    vec3 viewDirection  = normalize(u_ViewPos.xyz - worldPos);

    // Ambient / Indirect Diffuse
    vec3 ambient;
    if (u_DDGIEnabled == 1)
    {
        vec3 indirectIrradiance = sampleDDGIIrradiance(worldPos, normal);
        // Blend toward flat ambient when DDGI irradiance is still converging (near-zero)
        vec3 flatAmbient = u_AmbientStrength * u_LightColor.xyz * diffuseColor;
        float ddgiLuma = dot(indirectIrradiance, vec3(0.333));
        float blend = smoothstep(0.0, 0.05, ddgiLuma); // 0 when black → 1 when visible
        ambient = mix(flatAmbient, indirectIrradiance * diffuseColor, blend);
    }
    else
    {
        ambient = u_AmbientStrength * u_LightColor.xyz * diffuseColor;
    }

    // Diffuse (half-Lambert wrap, matches Lit.shader)
    float diff = max(dot(normal, lightDirection), 0.0);
    diff = diff * 0.5 + 0.5;
    vec3 diffuse = diff * u_LightColor.xyz * diffuseColor;

    // Specular (Blinn-Phong, matches Lit.shader)
    vec3 halfwayDir = normalize(lightDirection + viewDirection);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    vec3 specular = spec * u_LightColor.xyz * specularColor;

    // Shadow factor: combine ray-traced shadow mask with PCF shadow map
    float shadowMapFactor = sampleShadowMap(worldPos, normal, lightDirection);

	// Shadow map debug visualization
	if (u_DebugMode == 8) { o_Color = vec4(vec3(shadowMapFactor), 1.0); return; }
    if (u_DebugMode == 9)
    {
        int clusterID = getClusterID(worldPos);
        float clusterLoad = 0.0;
        if (u_ClusteredLightingEnabled == 1 && u_ClusterCountX > 0 && u_ClusterCountY > 0 && u_ClusterCountZ > 0)
            clusterLoad = float(clusters[clusterID].y) / float(max(u_MaxLightsPerCluster, 1));
        else
            clusterLoad = float(u_LocalLightCount) / float(max(u_MaxLightsPerCluster, 1));
        o_Color = vec4(vec3(clamp(clusterLoad, 0.0, 1.0)), 1.0);
        return;
    }
    float combinedShadow = shadow * shadowMapFactor;

    vec3 localLighting = accumulateLocalLights(worldPos, normal, viewDirection, diffuseColor, specularColor, shininess);

    vec3 result = ambient + (diffuse + specular) * combinedShadow + localLighting;
    o_Color = vec4(result, 1.0);
}
