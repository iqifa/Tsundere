#shader compute
#version 460
#extension GL_EXT_ray_query : require

// Vulkan 入口：硬件 Ray Query 求交（VK_KHR_ray_query + 加速结构）。OpenGL 不加载这个文件。
// 环境 = 常量颜色（没有天空盒时使用，避免未绑定的 cubemap 描述符）。

// sampler / image 必须在入口文件里写明 binding（见 PathTraceCommon.glsl）
layout(binding = 1, rgba32f) uniform image2D u_Accum;
#define PT_ACCUM_DECLARED
#include "include/PathTraceTraceRayQuery.glsl"
#include "include/PathTraceIntegrator.glsl"
