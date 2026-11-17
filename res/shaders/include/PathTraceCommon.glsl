#ifndef TS_PATH_TRACE_COMMON_GLSL
#define TS_PATH_TRACE_COMMON_GLSL

// ---------------------------------------------------------------------------
// GPU Path Tracing —— 两后端共享的数据与工具函数
//
// 射线 / 命中契约（OpenGL 软件 BVH 与 Vulkan Ray Query 完全一致）：
//   * PTRay.origin / direction 都在世界空间；direction 由调用方归一化，
//     t 因此就是世界空间距离：P = origin + t * direction。
//   * 有效区间是开区间 (tMin, tMax)。自相交靠沿几何法线偏移起点解决，tMin 取 0。
//   * 实例变换下：物体空间射线 = worldToObject * 世界射线，方向不归一化，
//     t 在两个空间相同 —— 非均匀缩放下距离仍然可以直接比较。
//   * PTHit.instance  = u_BVHInstances 下标（GL：TLAS 叶子顺序；VK：TLAS instanceIndex）
//     PTHit.primitive = 网格内三角形下标；全局三角形 = instance.triangleOffset + primitive
//     PTHit.bary      = (u, v)，P = (1-u-v)·v0 + u·v1 + v·v2（与 VK 规范一致）
//   * 不做背面剔除；正反面在表面重建里用几何法线与射线方向判定（镜像实例同样正确）。
//
// C++ 镜像：T_Core/src/Pipeline/PathTracing/PathTraceScene.h（static_assert 校验）。
// ---------------------------------------------------------------------------

#include "BVH.glsl"
#include "Lighting.glsl"   // Light 结构、DistanceAttenuation、BuildLightBasis（与光栅路径同一约定）

#define PT_BINDING_FRAME     0
#define PT_BINDING_ACCUM     1
#define PT_BINDING_MATERIALS 2
#define PT_BINDING_LIGHTS    3
#define PT_BINDING_SKY       4

#define PT_MATERIAL_DIFFUSE 0u
#define PT_MATERIAL_METAL   1u

#define PT_DEBUG_NONE       0u
#define PT_DEBUG_NORMAL     1u
#define PT_DEBUG_FACING     2u
#define PT_DEBUG_ALBEDO     3u
#define PT_DEBUG_UV         4u
#define PT_DEBUG_INSTANCE   5u
#define PT_DEBUG_DEPTH      6u

#define PT_INF 1e30

// 208 字节，std140。
layout(std140, binding = PT_BINDING_FRAME) uniform PerPass_PathTrace
{
    mat4  u_InvView;
    mat4  u_InvProj;
    vec4  u_CameraPos;
    uvec4 u_Frame;      // x = 已累积样本数, y = 本次每像素样本数, z = 最大反弹数, w = 帧随机种子
    uvec4 u_Config;     // x = 灯光数, y = 调试视图, z = 宽, w = 高
    vec4  u_Environment;// rgb = 无天空盒时的环境色, a = 环境强度
    vec4  u_Params;     // x = 单样本亮度上限(0 = 不限), y = 俄罗斯轮盘起始反弹, z = 调试深度范围
};

// u_Accum（binding 1, rgba32f image2D）与 u_Sky（binding 4, samplerCube）由入口 .shader
// 声明：OpenGL 路径的 ShaderCompiler 只认入口文件里写明的 sampler / image binding，
// include 里的会被自动重新分配，C++ 侧绑定的单元就对不上了。
#ifndef PT_ACCUM_DECLARED
#error The entry .shader must declare u_Accum and define PT_ACCUM_DECLARED
#endif

// 48 字节，std430。
struct PTMaterial
{
    vec4  baseColor;    // rgb 线性反照率 / 金属 F0
    vec4  emission;     // rgb = 自发光辐亮度（已乘强度）
    float roughness;    // 感知粗糙度
    float metallic;
    uint  type;         // PT_MATERIAL_*
    uint  flags;
};

layout(std430, binding = PT_BINDING_MATERIALS) readonly buffer PTMaterialBuffer
{
    PTMaterial u_Materials[];
};

layout(std430, binding = PT_BINDING_LIGHTS) readonly buffer PTLightBuffer
{
    Light u_Lights[];   // 与 DeferredLightPass::GPULight 同布局
};

struct PTRay
{
    vec3  origin;
    float tMin;
    vec3  direction;
    float tMax;
};

struct PTHit
{
    float t;
    uint  instance;
    uint  geometry;     // 每个 BLAS 只有一个几何，恒为 0
    uint  primitive;
    vec2  bary;
};

struct PTSurface
{
    vec3  position;
    vec3  geometricNormal;  // 已翻到入射一侧
    vec3  shadingNormal;    // 已翻到入射一侧，且与几何法线同半球
    vec2  uv;
    bool  frontFace;
    uint  material;
    uint  instance;
};

// 遍历出错（软件栈溢出）时置位，像素输出洋红色，不静默丢节点。
bool g_PTTraversalOverflow = false;

// ---------------------------------------------------------------------------
// 随机数：PCG hash。种子由 像素 × 样本号 × 帧种子 组合，
// 同一路径内再按“维度”依次推进状态。
// ---------------------------------------------------------------------------
uint PT_Pcg(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint PT_InitRng(uint pixel, uint sampleIndex, uint frameSeed)
{
    return PT_Pcg(pixel ^ PT_Pcg(sampleIndex ^ PT_Pcg(frameSeed + 0x9E3779B9u)));
}

float PT_Rand(inout uint state)
{
    state = PT_Pcg(state);
    return float(state >> 8) * (1.0 / 16777216.0);   // [0, 1)
}

// ---------------------------------------------------------------------------
// 采样工具
// ---------------------------------------------------------------------------
void PT_Basis(vec3 n, out vec3 t, out vec3 b)
{
    // Duff et al. 2017
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}

vec3 PT_CosineSample(vec3 n, float u1, float u2)
{
    float r = sqrt(u1);
    float phi = 2.0 * PI * u2;
    vec3 t, b;
    PT_Basis(n, t, b);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1)));
}

// 自相交偏移：沿“出射一侧”的几何法线挪一点，幅度随坐标量级增长。
vec3 PT_OffsetOrigin(vec3 p, vec3 ng, vec3 dir)
{
    float scale = max(1.0, max(abs(p.x), max(abs(p.y), abs(p.z))));
    return p + (dot(ng, dir) >= 0.0 ? ng : -ng) * (1e-4 * scale);
}

float PT_Luminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// ---------------------------------------------------------------------------
// 表面重建（命中后在 GPU 上完成，两后端共用）
// ---------------------------------------------------------------------------
PTSurface PT_ReconstructSurface(PTHit hit, vec3 rayDir)
{
    BVHInstance inst = u_BVHInstances[hit.instance];
    uint tri = inst.triangleOffset + hit.primitive;
    BVHTriangle T = u_BVHTriangles[tri];
    BVHTriangleAttrib A = u_BVHTriangleAttribs[tri];

    float u = hit.bary.x;
    float v = hit.bary.y;
    float w = 1.0 - u - v;

    vec3 pObj = T.v0 * w + T.v1 * u + T.v2 * v;

    // 法线矩阵 = M^-T。对几何法线用物体空间的 cross 再变换：
    // 镜像实例（det < 0）的绕序翻转不影响结果，与网格自带法线保持同一朝向约定。
    mat3 normalMatrix = transpose(mat3(inst.worldToObject));
    vec3 ng = normalize(normalMatrix * cross(T.v1 - T.v0, T.v2 - T.v0));
    vec3 ns = normalMatrix * (A.n0.xyz * w + A.n1.xyz * u + A.n2.xyz * v);
    float nsLen = length(ns);
    ns = nsLen > 1e-8 ? ns / nsLen : ng;

    PTSurface s;
    s.position = (inst.objectToWorld * vec4(pObj, 1.0)).xyz;
    s.frontFace = dot(ng, rayDir) < 0.0;
    if (!s.frontFace)
    {
        ng = -ng;
        ns = -ns;
    }
    // 插值法线可能越过几何面，翻回几何法线的半球
    if (dot(ns, ng) <= 0.0)
        ns = ng;
    s.geometricNormal = ng;
    s.shadingNormal = ns;
    s.uv = A.uv0 * w + A.uv1 * u + A.uv2 * v;
    s.material = inst.materialIndex;
    s.instance = hit.instance;
    return s;
}

// ---------------------------------------------------------------------------
// 环境
// ---------------------------------------------------------------------------
vec3 PT_SrgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 PT_Environment(vec3 dir)
{
#ifdef PT_USE_SKYBOX
    // 天空盒是 RGBA8 UNORM 存的 sRGB 字节，先解码成线性辐亮度。
    return PT_SrgbToLinear(textureLod(u_Sky, dir, 0.0).rgb) * u_Environment.a;
#else
    return u_Environment.rgb * u_Environment.a;
#endif
}

#endif
