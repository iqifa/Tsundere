#shader compute
#version 450
layout(local_size_x=1) in;
layout(binding=1, rgba32f) uniform image2D u_Accum;
layout(binding=4) uniform samplerCube u_Sky;
#define PT_ACCUM_DECLARED
#define PT_USE_SKYBOX
#include "../../res/shaders/include/PathTraceCommon.glsl"
void main() { imageStore(u_Accum, ivec2(0), vec4(PT_Environment(u_CameraPos.xyz), 1)); }
