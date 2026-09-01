#shader vertex
#version 430 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;

out vec2 v_TexCoord;

void main()
{
	v_TexCoord = a_TexCoord;
	gl_Position = vec4(a_Position, 0.0, 1.0);
}

#shader fragment
#version 430 core

in vec2 v_TexCoord;
layout(location = 0) out vec4 o_Color;

layout(binding = 10) uniform sampler2D u_SceneColor;
layout(binding = 11) uniform sampler2D u_ShadowMask;

void main()
{
	vec3 color = texture(u_SceneColor, v_TexCoord).rgb;
	float shadow = texture(u_ShadowMask, v_TexCoord).r;

	// Ambient floor: 30% brightness in shadow, 100% in light
	float lightFactor = 0.3 + 0.7 * shadow;
	o_Color = vec4(color * lightFactor, 1.0);
}