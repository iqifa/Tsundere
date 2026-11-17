#shader compute
#version 450

// 软件 BVH 求交入口：OpenGL 默认路径，Vulkan 在设备不支持 Ray Query 时回退到这里。
// 环境 = 常量颜色（没有天空盒时使用，避免未绑定的 cubemap 描述符）。

// sampler / image 必须在入口文件里写明 binding（见 PathTraceCommon.glsl）
layout(binding = 1, rgba32f) uniform image2D u_Accum;
#define PT_ACCUM_DECLARED
#include "include/PathTraceTraceBVH.glsl"
#include "include/PathTraceIntegrator.glsl"
