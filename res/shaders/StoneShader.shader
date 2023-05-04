#shader vertex
uniform float time;
uniform vec2 mouse;
uniform vec2 resolution;
highp float rand(vec2 co) {

    highp float a = 12.9898;
    highp float b = 78.233;
    highp float c = 43758.5453;
    highp float dt = dot(co.xy, vec2(a, b));
    highp float sn = mod(dt, 3.14159);
    return fract((sin(sn) * c) + time * (2.1875));
}

void main(void) {

    vec2 position = (gl_FragCoord.xy / resolution.xy);

    vec2 pos = vec2(position.x, position.y);

    gl_FragColor = vec4(rand(pos), rand(pos + 1.0), rand(pos + 2.0), 1.0);
}