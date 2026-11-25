#shader compute
#version 450
#define PT_ACCUM_DECLARED
#include "../../res/shaders/include/PathTraceCommon.glsl"
layout(local_size_x=64) in;
layout(std140,binding=6) uniform TestSurface {
    vec4 baseMetal;
    vec4 positionRough;
    vec4 normal;
    vec4 viewDirection;
    uvec4 testConfig; // sample count, sample-BSDF mode, blocker, shadow testing
};
layout(std430,binding=7) writeonly buffer TestResults { vec4 results[]; };
bool PT_Occluded(PTRay ray) { return testConfig.z != 0u; }
#include "../../res/shaders/include/PathTraceDirect.glsl"
void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= testConfig.x) return;
    PTSurface s;
    s.position=positionRough.xyz; s.shadingNormal=normal.xyz; s.geometricNormal=normal.xyz;
    PTMaterial m;
    m.baseColor=vec4(baseMetal.rgb,1); m.metallic=baseMetal.w; m.roughness=positionRough.w;
    uint rng=PT_InitRng(index,0u,123u);
    vec3 color;
    if (testConfig.y == 0u) color=PT_DirectLighting(s,m,viewDirection.xyz,rng,testConfig.w != 0u);
    else {
        vec3 L,weight;
        color=PT_SampleBsdf(m,s,viewDirection.xyz,rng,L,weight) ? weight : vec3(0);
    }
    results[index]=vec4(color,1);
}
