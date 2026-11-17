#shader compute
#version 450

// 软件 BVH 求交入口：OpenGL 默认路径，Vulkan 在设备不支持 Ray Query 时回退到这里。
// 环境 = 天空盒（binding 4 的 samplerCube）。
#define PT_USE_SKYBOX
// sampler / image 必须在入口文件里写明 binding（见 PathTraceCommon.glsl）
layout(binding = 1, rgba32f) uniform image2D u_Accum;
#define PT_ACCUM_DECLARED
layout(binding = 4) uniform samplerCube u_Sky;
#include "include/PathTraceTraceBVH.glsl"
#include "include/PathTraceIntegrator.glsl"
