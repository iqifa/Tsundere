#shader compute
#version 450
layout(local_size_x=1) in;
layout(binding=1, rgba32f) uniform image2D outputColor;
layout(std140, binding=0) uniform TestData { vec4 uvTiling; vec4 offsetTint; uvec4 info; };
#include "../../res/shaders/include/PathTraceMaterial.glsl"
void main() {
    vec2 uv = MaterialUV(uvTiling.xy, vec4(uvTiling.zw, offsetTint.xy));
    imageStore(outputColor, ivec2(0), PT_SampleBaseColor(info, uv) * vec4(offsetTint.zww, 1));
}
