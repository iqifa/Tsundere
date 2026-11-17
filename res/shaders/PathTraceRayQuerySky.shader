#shader compute
#version 460
#extension GL_EXT_ray_query : require

// Vulkan 入口：硬件 Ray Query 求交（VK_KHR_ray_query + 加速结构）。OpenGL 不加载这个文件。
// 环境 = 天空盒（binding 4 的 samplerCube）。
#define PT_USE_SKYBOX
// sampler / image 必须在入口文件里写明 binding（见 PathTraceCommon.glsl）
layout(binding = 1, rgba32f) uniform image2D u_Accum;
#define PT_ACCUM_DECLARED
layout(binding = 4) uniform samplerCube u_Sky;
#include "include/PathTraceTraceRayQuery.glsl"
#include "include/PathTraceIntegrator.glsl"
