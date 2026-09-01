#shader compute
#version 430 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

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

layout(std430, binding = 11) writeonly buffer ClusterMetaBuffer
{
    uvec4 clusters[];
};

layout(std430, binding = 12) writeonly buffer ClusterLightIndexBuffer
{
    uint clusterLightIndices[];
};

layout(std140, binding = 0) uniform PerPass_ClusterCull
{
    mat4  u_View;             // offset   0
    mat4  u_InvProj;          // offset  64
    vec2  u_ViewportSize;     // offset 128
    int   u_TileSize;         // offset 136
    int   u_ClusterCountX;    // offset 140
    int   u_ClusterCountY;    // offset 144
    int   u_ClusterCountZ;    // offset 148
    int   u_MaxLightsPerCluster; // offset 152
    int   u_LocalLightCount;  // offset 156
    float u_Near;             // offset 160
    float u_Far;              // offset 164
};

vec3 screenToView(vec2 screen, float viewDepth)
{
    vec2 uv = screen / max(u_ViewportSize, vec2(1.0));
    vec4 clip = vec4(uv * 2.0 - 1.0, -1.0, 1.0);
    vec4 view = u_InvProj * clip;
    vec3 ray = view.xyz / max(abs(view.w), 0.0001);
    ray /= max(-ray.z, 0.0001);
    return ray * viewDepth;
}

bool sphereIntersectsAABB(vec3 center, float radius, vec3 bmin, vec3 bmax)
{
    vec3 closest = clamp(center, bmin, bmax);
    vec3 delta = center - closest;
    return dot(delta, delta) <= radius * radius;
}

void main()
{
    uvec3 cid = gl_GlobalInvocationID.xyz;
    if (cid.x >= uint(u_ClusterCountX) || cid.y >= uint(u_ClusterCountY) || cid.z >= uint(u_ClusterCountZ))
        return;

    uint clusterID = cid.z * uint(u_ClusterCountX * u_ClusterCountY) + cid.y * uint(u_ClusterCountX) + cid.x;
    uint baseOffset = clusterID * uint(u_MaxLightsPerCluster);

    vec2 minScreen = vec2(cid.xy) * float(u_TileSize);
    vec2 maxScreen = min(minScreen + vec2(float(u_TileSize)), u_ViewportSize);

    float z0 = mix(u_Near, u_Far, float(cid.z) / float(max(u_ClusterCountZ, 1)));
    float z1 = mix(u_Near, u_Far, float(cid.z + 1u) / float(max(u_ClusterCountZ, 1)));

    vec3 corners[8];
    corners[0] = screenToView(vec2(minScreen.x, minScreen.y), z0);
    corners[1] = screenToView(vec2(maxScreen.x, minScreen.y), z0);
    corners[2] = screenToView(vec2(minScreen.x, maxScreen.y), z0);
    corners[3] = screenToView(vec2(maxScreen.x, maxScreen.y), z0);
    corners[4] = screenToView(vec2(minScreen.x, minScreen.y), z1);
    corners[5] = screenToView(vec2(maxScreen.x, minScreen.y), z1);
    corners[6] = screenToView(vec2(minScreen.x, maxScreen.y), z1);
    corners[7] = screenToView(vec2(maxScreen.x, maxScreen.y), z1);

    vec3 bmin = corners[0];
    vec3 bmax = corners[0];
    for (int i = 1; i < 8; i++)
    {
        bmin = min(bmin, corners[i]);
        bmax = max(bmax, corners[i]);
    }

    uint count = 0u;
    for (int i = 0; i < u_LocalLightCount; i++)
    {
        vec4 lightView = u_View * vec4(lights[i].positionRadius.xyz, 1.0);
        float radius = lights[i].positionRadius.w;

        if (sphereIntersectsAABB(lightView.xyz, radius, bmin, bmax))
        {
            if (count < uint(u_MaxLightsPerCluster))
            {
                clusterLightIndices[baseOffset + count] = uint(i);
                count++;
            }
        }
    }

    clusters[clusterID] = uvec4(baseOffset, count, 0u, 0u);
}
