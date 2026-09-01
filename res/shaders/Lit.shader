#shader vertex
#version 420 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

layout(std140, binding = 0) uniform PerDraw_Geometry
{
    mat4 MVP_matrix;
    mat4 model;
    mat4 prevModel;
    mat4 viewProj;
    mat4 prevViewProj;
};

layout(location = 0) out vec3 v_FragPos;
layout(location = 1) out vec2 v_TexCoords;
layout(location = 2) out vec3 v_Normal;
layout(location = 3) out vec3 v_Tangent;
layout(location = 4) out vec3 v_Bitangent;
layout(location = 5) out vec4 v_CurrentClipPos;
layout(location = 6) out vec4 v_PreviousClipPos;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    v_FragPos = worldPos.xyz;
    v_TexCoords = aTexCoords;
    v_Normal = normalize(mat3(transpose(inverse(model))) * aNormal);
    v_Tangent = normalize(mat3(model) * aTangent);
    v_Bitangent = normalize(mat3(model) * aBitangent);

    gl_Position = MVP_matrix * vec4(aPos, 1.0);
    v_CurrentClipPos = viewProj * worldPos;
    v_PreviousClipPos = prevViewProj * prevModel * vec4(aPos, 1.0);
}

#shader fragment
#version 420 core

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec2 MotionVector;

layout(location = 0) in vec3 v_FragPos;
layout(location = 1) in vec2 v_TexCoords;
layout(location = 2) in vec3 v_Normal;
layout(location = 3) in vec3 v_Tangent;
layout(location = 4) in vec3 v_Bitangent;
layout(location = 5) in vec4 v_CurrentClipPos;
layout(location = 6) in vec4 v_PreviousClipPos;

layout(std140, binding = 1) uniform PerFrame_Geometry
{
    vec4  lightDir;             // vec3 → vec4
    vec4  lightColor;           // vec3 → vec4
    float ambientStrength;
    float _pad0;
    float _pad1;
    float _pad2;
    vec4  viewPos;              // vec3 → vec4
    mat4  u_LightViewProj;
    int   hasNormalMap;
    int   u_ShadowMapEnabled;
    vec2  u_ShadowMapSize;
    float u_LightSize;
    int   _pad3[3];
};

layout(binding = 10) uniform sampler2D texture_diffuse1;
layout(binding = 11) uniform sampler2D texture_specular1;
layout(binding = 12) uniform sampler2D texture_normal1;
layout(binding = 13) uniform sampler2D u_ShadowMap;

const float shininess = 32.0;


float interleavedGradientNoise(vec2 position_screen) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(position_screen, magic.xy)));
}

// 2. 16个泊松盘采样点 (分布更均匀的圆形采样模式)
const vec2 poissonDisk[16] = vec2[](
   vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ),
   vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ),
   vec2( -0.91588581, 0.45771432 ), vec2( -0.81544232, -0.87912464 ),
   vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ),
   vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ),
   vec2( -0.26496911, -0.41893023 ), vec2( 0.79197514, 0.19090188 ),
   vec2( -0.24188840, 0.99706507 ), vec2( -0.81409955, 0.91437590 ),
   vec2( 0.19984126, 0.78641367 ), vec2( 0.14383161, -0.14100790 )
);

// ================================================================
// PCF shadow map sampling (Percentage Closer Filtering)
// ================================================================
float sampleShadowMap(vec3 worldPos, vec3 normal)
{
	if (u_ShadowMapEnabled == 0)
		return 1.0;

	vec3 lightDirection = normalize(-lightDir.xyz);

	// Normal offset: push sample point outward along normal to avoid self-shadowing.
	// Only offset surfaces facing the light -- backfaces get no offset (they are naturally in shadow).
	float NdotL_shadow = max(dot(normal, lightDirection), 0.0);
	if (NdotL_shadow <= 0.0)
		return 0.0;
	float normalOffset = 0.02 * (1.0 - NdotL_shadow) + 0.005;
	vec3 biasedPos = worldPos + normal * normalOffset;

	// Transform world position to light clip space
	vec4 lightClip = u_LightViewProj * vec4(biasedPos, 1.0);
	if (lightClip.w <= 0.0)
		return 1.0;

	vec3 lightNDC = lightClip.xyz / lightClip.w;

	// Transform NDC [-1,1] to UV [0,1]
	vec2 uv = lightNDC.xy * 0.5 + 0.5;

	// Outside shadow map bounds to fully lit
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
		return 1.0;

	// Depth bias: constant + slope-scale (10x larger for light frustum depth range)
	float bias = max(0.005 * (1.0 - NdotL_shadow), 0.001);

	float fragDepth = lightNDC.z * 0.5 + 0.5 - bias;
	vec2 texelSize = 1.0 / u_ShadowMapSize;

	// ---- 1. Blocker Search ----
	const int  BLOCKER_SAMPLES = 16;
	const float BLOCKER_SEARCH_UV = 0.015; // ~30 texels

	float avgBlockerDepth = 0.0;
	int   blockerCount    = 0;

	float n1 = interleavedGradientNoise(gl_FragCoord.xy);
	float a1 = n1 * 6.2831853;
	mat2  r1 = mat2(cos(a1), -sin(a1), sin(a1), cos(a1));

	for (int i = 0; i < BLOCKER_SAMPLES; i++)
	{
		vec2  offset = r1 * poissonDisk[i] * BLOCKER_SEARCH_UV;
		float depth  = texture(u_ShadowMap, uv + offset).r;
		if (fragDepth > depth)
		{
			avgBlockerDepth += depth;
			blockerCount++;
		}
	}

	// No blockers -> fully lit
	if (blockerCount == 0)
		return 1.0;

	avgBlockerDepth /= float(blockerCount);

	// ---- 2. Penumbra Estimation ----
	// penumbraWidth = (receiver - blocker) / blocker * lightSize
	float penumbraRatio  = (fragDepth - avgBlockerDepth) / max(avgBlockerDepth, 0.0001);
	float filterTexels   = penumbraRatio * u_LightSize;
	filterTexels         = clamp(filterTexels, 1.0, 80.0);
	float filterRadiusUV = filterTexels / u_ShadowMapSize.x;

	// ---- 3. Adaptive PCF ----
	const int PCF_SAMPLES = 16;
	float shadow = 0.0;

	float n2 = interleavedGradientNoise(gl_FragCoord.xy + vec2(0.5, 0.5));
	float a2 = n2 * 6.2831853;
	mat2  r2 = mat2(cos(a2), -sin(a2), sin(a2), cos(a2));

	for (int i = 0; i < PCF_SAMPLES; i++)
	{
		vec2  offset = r2 * poissonDisk[i] * filterRadiusUV;
		float depth  = texture(u_ShadowMap, uv + offset).r;
		shadow += fragDepth <= depth ? 1.0 : 0.0;
	}
	shadow /= float(PCF_SAMPLES);

	return shadow;
}

void main()
{
    // TEMP DEBUG: visualize normal in fragment
    FragColor = vec4(0.5 + 0.5 * normalize(v_Normal), 1.0);
    MotionVector = vec2(0.0);
    return;
    vec3 normal;
    if (hasNormalMap == 1)
    {
        vec3 normalTex = texture(texture_normal1, v_TexCoords).rgb;
        normalTex = normalize(normalTex * 2.0 - 1.0);
        vec3 T = normalize(v_Tangent);
        vec3 B = normalize(v_Bitangent);
        vec3 N = normalize(v_Normal);
        mat3 TBN = mat3(T, B, N);
        normal = normalize(TBN * normalTex);
    }
    else
    {
        normal = normalize(v_Normal);
    }

    vec3 lightDirection = normalize(-lightDir.xyz);
    vec3 viewDirection = normalize(viewPos.xyz - v_FragPos);

    vec3 diffuseColor = texture(texture_diffuse1, v_TexCoords).rgb;
    vec3 specularColor = texture(texture_specular1, v_TexCoords).rgb;

    vec3 ambient = ambientStrength * lightColor.xyz * diffuseColor;
    float diff = max(dot(normal, lightDirection), 0.0);
    diff=diff*0.5+0.5;
    vec3 diffuse = diff * lightColor.xyz * diffuseColor;
    vec3 halfwayDir = normalize(lightDirection + viewDirection);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    vec3 specular = spec * lightColor.xyz * specularColor;

    // Shadow map PCSS
    float shadowMapFactor = sampleShadowMap(v_FragPos, normal);

    vec3 result = ambient + (diffuse + specular) * shadowMapFactor;
    FragColor = vec4(result, 1.0);

    vec3 currentNDC = v_CurrentClipPos.xyz / v_CurrentClipPos.w;
    vec3 previousNDC = v_PreviousClipPos.xyz / v_PreviousClipPos.w;
    MotionVector = (currentNDC - previousNDC).xy * 0.5;
}

