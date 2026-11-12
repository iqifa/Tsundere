#shader vertex
#version 450 core
layout(push_constant) uniform PreviewSettings { vec4 settings; } pc;
layout(location=0) out vec2 uv;
layout(location=1) flat out vec4 settings;
void main() {
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0, 1);
    settings = pc.settings;
}
#shader fragment
#version 450 core
layout(location=0) in vec2 uv;
layout(location=1) flat in vec4 settings;
layout(binding=0) uniform sampler2D source;
layout(location=0) out vec4 color;
void main() {
    ivec2 size = textureSize(source, 0);
    vec4 value = texelFetch(source, clamp(ivec2(uv * vec2(size)), ivec2(0), size-1), 0);
    // -1 is a real format conversion: let the attachment quantize/clamp the data.
    int channel = int(settings.x);
    if (channel < 0) { color = value; return; }
    // 0 RGB, 1 RGBA, 2 R, 3 G, 4 B, 5 A. Single channels display as grayscale.
    vec3 rgb = channel >= 2 ? vec3(value[channel-2]) : value.rgb;
    rgb = (rgb - settings.y) / max(settings.z-settings.y, 0.000001);
    color = vec4(rgb, channel == 1 ? value.a : 1.0);
}
