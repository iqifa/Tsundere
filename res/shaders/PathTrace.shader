#shader compute
#version 430 core
#extension GL_ARB_bindless_texture : require

layout(local_size_x = 8, local_size_y = 8) in;

// Ping-pong accumulation
layout(binding = 0, rgba32f) uniform readonly image2D u_AccumIn;
layout(binding = 1, rgba32f) uniform writeonly image2D u_AccumOut;

// Camera
layout(std140, binding = 0) uniform PerPass_PathTrace
{
    mat4  u_InvView;             // offset   0
    mat4  u_InvProj;             // offset  64
    vec4  u_CameraPos;           // offset 128 (vec3 → vec4)
    vec2  u_Resolution;          // offset 144
    float u_SampleIndex;         // offset 152
    float u_FrameSeed;           // offset 156
};

// BVH data (std430, matches BVH.h)
struct Triangle
{
	vec4 v0, v1, v2;
	vec4 n0, n1, n2;
	vec4 uv0, uv1, uv2;
};

struct BVHNode
{
	vec4 bboxMin;
	vec4 bboxMax;
};

layout(std430, binding = 3) buffer TriBuffer { Triangle triangles[]; } u_Triangles;
layout(std430, binding = 4) buffer BVHNodeBuffer { BVHNode nodes[]; } u_BVH;

// Material data
struct Material
{
	vec4 albedo;   // rgb=color, a=roughness
	vec4 emission; // rgb=emissive, a=metallic
	uvec2 diffuseHandle; // bindless texture handle (0 = use albedo color)
};

layout(std430, binding = 5) buffer MatBuffer { Material materials[]; } u_Materials;

// Skybox
layout(binding = 6) uniform samplerCube u_SkyBox;

// Constants
const float PI = 3.14159265359;
const float INF = 1e30;
const int MAX_BOUNCES = 6;

// Halton low-discrepancy sequence (O(1/N) convergence vs O(1/sqrt(N)) for pure random)
float halton(int index, int base)
{
	float f = 1.0;
	float r = 0.0;
	int i = index;
	while (i > 0)
	{
		f = f / float(base);
		r = r + f * float(i % base);
		i = i / base;
	}
	return r;
}

// Scrambled Halton with Cranley-Patterson rotation per pixel
vec2 halton2d(int sampleIdx, int pixelSeed)
{
	float hx = halton(sampleIdx + 1, 2);
	float hy = halton(sampleIdx + 1, 3);
	// Decorrelate across pixels with irrational rotation
	float rx = fract(hx + float(pixelSeed) * 0.6180339887498949);
	float ry = fract(hy + float(pixelSeed) * 0.3183098861837907);
	return vec2(rx, ry);
}

// Simple PCG for Russian roulette (uniform random is fine for this)
uint pcgHash(uint seed)
{
	uint state = seed * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float rand01(inout uint seed)
{
	seed = pcgHash(seed);
	return float(seed) / float(0xFFFFFFFFu);
}

// Ray-AABB slab test
bool rayAABBIntersect(vec3 ro, vec3 invDir, vec3 bmin, vec3 bmax, out float tNear, out float tFar)
{
	vec3 t0 = (bmin - ro) * invDir;
	vec3 t1 = (bmax - ro) * invDir;
	vec3 tmin = min(t0, t1);
	vec3 tmax = max(t0, t1);
	tNear = max(max(tmin.x, tmin.y), tmin.z);
	tFar  = min(min(tmax.x, tmax.y), tmax.z);
	return tNear <= tFar && tFar > 0.0;
}

// Moller-Trumbore
bool rayTriangleIntersect(vec3 ro, vec3 rd, Triangle tri, out float t, out vec2 uv)
{
	vec3 e1 = tri.v1.xyz - tri.v0.xyz;
	vec3 e2 = tri.v2.xyz - tri.v0.xyz;
	vec3 h = cross(rd, e2);
	float a = dot(e1, h);

	if (abs(a) < 0.000001)
		return false;

	float f = 1.0 / a;
	vec3 s = ro - tri.v0.xyz;
	float u = f * dot(s, h);

	if (u < 0.0 || u > 1.0)
		return false;

	vec3 q = cross(s, e1);
	float v = f * dot(rd, q);

	if (v < 0.0 || u + v > 1.0)
		return false;

	t = f * dot(e2, q);
	uv = vec2(u, v);
	return t > 0.001;
}

// Closest-hit BVH traversal
bool intersectBVH(vec3 ro, vec3 rd, out float tHit, out vec3 normal, out vec2 texUV, out int matIdx)
{
	vec3 invDir = 1.0 / rd;
	int stack[32];
	int ptr = 0;
	stack[ptr++] = 0;

	bool hit = false;
	float closest = INF;

	while (ptr > 0)
	{
		int idx = stack[--ptr];
		BVHNode node = u_BVH.nodes[idx];

		float tNear, tFar;
		if (!rayAABBIntersect(ro, invDir, node.bboxMin.xyz, node.bboxMax.xyz, tNear, tFar))
			continue;
		if (tNear > closest)
			continue;

		if (node.bboxMin.w < 0.0)
		{
			// Leaf
			int firstTri = int(-node.bboxMin.w) - 1;
			int triCount = int(node.bboxMax.w);

			for (int i = firstTri; i < firstTri + triCount; i++)
			{
				float t;
				vec2 uv;
				if (rayTriangleIntersect(ro, rd, u_Triangles.triangles[i], t, uv))
				{
					if (t < closest)
					{
						closest = t;
						tHit = t;

						// Barycentric interpolation: normal + texcoord
						Triangle tri = u_Triangles.triangles[i];
						float w = 1.0 - uv.x - uv.y;
						normal = normalize(tri.n0.xyz * w + tri.n1.xyz * uv.x + tri.n2.xyz * uv.y);
						texUV  = tri.uv0.xy * w + tri.uv1.xy * uv.x + tri.uv2.xy * uv.y;

						matIdx = int(tri.n0.w);
						hit = true;
					}
				}
			}
		}
		else
		{
			int left = int(node.bboxMin.w);
			int right = int(node.bboxMax.w);
			if (ptr + 2 <= 32)
			{
				stack[ptr++] = left;
				stack[ptr++] = right;
			}
		}
	}

	return hit;
}

// Cosine-weighted hemisphere sampling
vec3 sampleHemisphere(vec3 normal, float r1, float r2)
{
	float phi = 2.0 * PI * r1;
	float cosTheta = sqrt(1.0 - r2);
	float sinTheta = sqrt(r2);

	vec3 w = normal;
	vec3 u = abs(w.x) > 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 v = normalize(cross(w, u));
	u = cross(v, w);

	return normalize(u * cos(phi) * sinTheta + v * sin(phi) * sinTheta + w * cosTheta);
}

// Path tracing
// Path tracing
vec3 pathTrace(vec3 ro, vec3 rd, int sampleIdx, int pixelIdx, inout uint seed)
{
    vec3 throughput = vec3(1.0);
    vec3 radiance = vec3(0.0);

    for (int bounce = 0; bounce < MAX_BOUNCES; bounce++)
    {
        float tHit;
        vec3 normal;
        vec2 texUV;
        int matIdx;

        if (!intersectBVH(ro, rd, tHit, normal, texUV, matIdx))
        {
            // Miss: sample skybox
            radiance += throughput * texture(u_SkyBox, rd).rgb;
            break;
        }

        vec3 hitPoint = ro + rd * tHit;

        // 【新增 1：法线修正】确保法线永远迎着入射光线，防止自交卡死在模型内部产生死黑
        vec3 ffnormal = dot(normal, rd) < 0.0 ? normal : -normal;

		
		// return ffnormal * 0.5 + 0.5;


        Material mat = u_Materials.materials[matIdx];

        // Resolve albedo: sample texture if available, else use base color
        vec3 albedo;
        if (mat.diffuseHandle != uvec2(0))
        {
            sampler2D diffuseTex = sampler2D(mat.diffuseHandle);
            albedo = mat.albedo.rgb * texture(diffuseTex, texUV).rgb;
        }
        else
        {
            albedo = mat.albedo.rgb;
        }

        // Add emission
        radiance += throughput * mat.emission.rgb;

        // ==========================================================
        // 【新增 2：显式直接光照 (定向光/太阳光)】
        // 这里硬编码了一个光源，后续你可以把它们写进 Uniform 由 C++ 传入
        vec3 lightDir = normalize(vec3(0.4, 0.8, 0.5));
        vec3 lightColor = vec3(2.0);
        float NdotL = max(dot(ffnormal, lightDir), 0.0);

        float shadowTHit;
        vec3 shadowNormal;
        vec2 dummyUV;
        int shadowMatIdx;
        if (NdotL > 0.0 && !intersectBVH(hitPoint + ffnormal * 0.001, lightDir, shadowTHit, shadowNormal, dummyUV, shadowMatIdx))
        {
            radiance += throughput * albedo * (1.0 / PI) * lightColor * NdotL;
        }

        // Sky ambient: only on first bounce as fill light.
        // Indirect bounces get environment light from miss rays.
        if (bounce == 0)
            radiance += throughput * albedo * texture(u_SkyBox, ffnormal).rgb * 0.15;
        // ==========================================================

        // Diffuse: Halton low-discrepancy hemisphere sampling
        int dimOffset = int(uint(pixelIdx) ^ uint(bounce * 127));
        vec2 h = halton2d(sampleIdx, dimOffset);
        vec3 wi = sampleHemisphere(ffnormal, h.x, h.y);

        // BRDF: albedo/pi, cosine-weighted → just albedo
        throughput *= albedo;

        // Offset to avoid self-intersection
        // 【修改】：沿修正后的法线进行偏移，彻底避免背面黑斑
        ro = hitPoint + ffnormal * 0.001; 
        rd = wi;

        // Russian roulette
        float p = max(throughput.r, max(throughput.g, throughput.b));
        if (bounce > 2)
        {
            if (rand01(seed) > p)
                break;
            throughput /= p;
        }
    }

    return radiance;
}
void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(u_Resolution.x) || pixel.y >= int(u_Resolution.y))
		return;

	// Seed from pixel + frame
	int pixelIdx = pixel.y * int(u_Resolution.x) + pixel.x;
	uint seed = pcgHash(uint(pixelIdx) ^ uint(u_FrameSeed));

	// Halton low-discrepancy jitter (converges O(1/N) vs random O(1/sqrt(N)))
	int sampleIdx = int(u_SampleIndex);
	vec2 jitter = halton2d(sampleIdx, pixelIdx);
	vec2 uv = (vec2(pixel) + jitter) / u_Resolution;

	// Ray generation
	vec2 ndc = uv * 2.0 - 1.0;
	vec4 clip = vec4(ndc, -1.0, 1.0);
	vec4 view = u_InvProj * clip;
	view /= view.w;
	vec4 world = u_InvView * vec4(view.xyz, 1.0);

	vec3 ro = u_CameraPos.xyz;
	vec3 rd = normalize(world.xyz - ro);

	vec3 color = pathTrace(ro, rd, sampleIdx, pixelIdx, seed);

	// Clamp fireflies
	color = min(color, vec3(10.0));

	// Progressive accumulation
	float N = u_SampleIndex + 1.0;
	vec4 accum = vec4(0.0);
	if (u_SampleIndex > 0.0)
		accum = imageLoad(u_AccumIn, pixel);

	vec3 averaged = mix(accum.rgb, color, 1.0 / N);

	// Reinhard tone mapping
	// averaged = averaged / (averaged + vec3(1.0));

	imageStore(u_AccumOut, pixel, vec4(averaged, 1.0));
}
