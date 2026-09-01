#shader compute
#version 430 core
#extension GL_ARB_bindless_texture : require

layout(local_size_x = 64) in;

// ============================================================
// Probe position/radius SSBO (matches DDGI::GPUProbeRayData)
// ============================================================
struct ProbeRayData
{
	vec4 Position_Radius;   // xyz = world position, w = influence radius
};
layout(std430, binding = 0) readonly buffer ProbeBuffer
{
	ProbeRayData probes[];
} u_Probes;

// ============================================================
// Output atlases (image load/store)
// ============================================================
layout(binding = 1, rgba16f) uniform writeonly image2D u_IrradianceOut;
layout(binding = 2, r16f)    uniform writeonly image2D u_DepthOut;

// Previous frame atlases (temporal blending)
layout(binding = 3, rgba16f) uniform readonly image2D u_IrradianceHistory;
layout(binding = 4, r16f)    uniform readonly image2D u_DepthHistory;

// ============================================================
// BVH data (std430, matches BVH.h — reused from PathTrace.shader)
// ============================================================
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
layout(std430, binding = 5) buffer TriBuffer    { Triangle triangles[]; } u_Triangles;
layout(std430, binding = 6) buffer BVHNodeBuffer { BVHNode nodes[];    } u_BVH;

// Material data (same layout as PathTrace.shader)
struct Material {
	vec4 albedo;   // rgb=color, a=roughness
	vec4 emission; // rgb=emissive, a=metallic
	uvec2 diffuseHandle; // bindless texture handle (0 = fallback to albedo)
};
layout(std430, binding = 7) buffer MatBuffer { Material materials[]; } u_Materials;

// ============================================================
// Skybox
// ============================================================
layout(binding = 8) uniform samplerCube u_SkyBox;

layout(std140, binding = 0) uniform PerPass_DDGI
{
    int   u_TotalProbes;       // offset   0
    int   u_ProbesPerRow;      // offset   4
    int   u_RaysPerProbe;      // offset   8
    int   u_ProbesPerUpdate;   // offset  12
    int   u_ProbeOffset;       // offset  16
    float u_Hysteresis;        // offset  20
    float u_FrameSeed;         // offset  24
    // pad 8B  → 32 total
};

// ============================================================
// Constants
// ============================================================
const float PI = 3.14159265359;
const float INF = 1e30;

// ============================================================
// Ray-AABB slab test (from PathTrace.shader)
// ============================================================
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

// ============================================================
// Moller-Trumbore ray-triangle intersection
// ============================================================
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

// ============================================================
// Closest-hit BVH traversal (from PathTrace.shader)
// ============================================================
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
			// Leaf node
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
						Triangle tri = u_Triangles.triangles[i];
						float w = 1.0 - uv.x - uv.y;
						normal = normalize(tri.n0.xyz * w + tri.n1.xyz * uv.x + tri.n2.xyz * uv.y);
						matIdx = int(tri.n0.w);
						texUV = tri.uv0.xy * w + tri.uv1.xy * uv.x + tri.uv2.xy * uv.y;
						hit = true;
					}
				}
			}
		}
		else
		{
			int left  = int(node.bboxMin.w);
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

// ============================================================
// Octahedral encoding: maps unit direction -> [0,1]^2
// ============================================================
vec2 OctEncode(vec3 n)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	vec2 oct = n.z >= 0.0 ? vec2(n.x, n.y)
	                      : (1.0 - abs(vec2(n.y, n.x))) * vec2(sign(n.x), sign(n.y));
	return oct * 0.5 + 0.5;
}

// ============================================================
// Octahedral decoding: maps [0,1]^2 -> unit direction
// ============================================================
vec3 OctDecode(vec2 e)
{
	e = e * 2.0 - 1.0;
	vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
	float t = clamp(-n.z, 0.0, 1.0);
	n.x += n.x >= 0.0 ? -t : t;
	n.y += n.y >= 0.0 ? -t : t;
	return normalize(n);
}

// ============================================================
// Fibonacci sphere: generate well-distributed direction on unit sphere
// ============================================================
vec3 FibonacciSphere(int i, int n)
{
	float phi   = acos(1.0 - 2.0 * (float(i) + 0.5) / float(n));
	float theta = 2.3999632 * float(i);  // pi * (3 - sqrt(5))
	return vec3(sin(phi) * cos(theta),
	            sin(phi) * sin(theta),
	            cos(phi));
}

// ============================================================
// Main compute entry point
// One thread = one ray.  Rays are packed: [probe][ray].
// ============================================================
void main()
{
	uint globalID = gl_GlobalInvocationID.x;

	int rayIndex    = int(globalID);
	int probeIndex  = rayIndex / u_RaysPerProbe;
	int rayInProbe  = rayIndex - probeIndex * u_RaysPerProbe;

	if (probeIndex >= u_ProbesPerUpdate)  // guard against excess threads (ProbesPerUpdate * RaysPerProbe < total dispatched)
		return;

	// Map to global probe index (round-robin)
	int globalProbeIdx = u_ProbeOffset + probeIndex;
	if (globalProbeIdx >= u_TotalProbes)
		return;

	ProbeRayData probe = u_Probes.probes[globalProbeIdx];
	vec3 probePos     = probe.Position_Radius.xyz;
	float probeRadius = probe.Position_Radius.w;

	// Generate ray direction using Fibonacci sphere (deterministic + well-stratified)
	// Offset by frame seed to decorrelate across frames
	vec3 rayDir = FibonacciSphere(rayInProbe, u_RaysPerProbe);

	// Trace ray through BVH from probe position
	vec3 ro = probePos;
	vec3 rd = rayDir;

	float  tHit;
	vec3   hitNormal;
	int    matIdx;
	vec2   texUV;
	bool   hit = intersectBVH(ro, rd, tHit, hitNormal, texUV, matIdx);

	vec3   hitRadiance = vec3(0.0);
	float  hitDistance = probeRadius * 2.0;

		if (hit && tHit < probeRadius * 2.0)
		{
			// Hit geometry: read material albedo at hit point
			Material mat = u_Materials.materials[matIdx];
			vec3 albedo;
			if (mat.diffuseHandle != uvec2(0)) {
				sampler2D diffuseTex = sampler2D(mat.diffuseHandle);
				albedo = mat.albedo.rgb * texture(diffuseTex, texUV).rgb;
			} else {
				albedo = mat.albedo.rgb;
			}
			// Simple diffuse: albedo modulated by reverse-ray direction
			hitRadiance = albedo * max(dot(hitNormal, -rd), 0.0);
			hitDistance = tHit;
		}
		else
		{
			// Miss: sky light reaches probe directly
			hitRadiance = texture(u_SkyBox, rayDir).rgb;
			hitDistance = probeRadius * 2.0;
		}

	// Octahedral encode the ray direction to find atlas texel coordinates
	vec2 octUV = OctEncode(rayDir);

	// Irradiance atlas: 8x8 octahedral map per probe
	int octTexelX = int(octUV.x * 7.0 + 0.5);
	int octTexelY = int(octUV.y * 7.0 + 0.5);

	// Depth atlas: 16x16 per probe
	int depthTexelX = int(octUV.x * 15.0 + 0.5);
	int depthTexelY = int(octUV.y * 15.0 + 0.5);

	// Atlas coordinates
	int probeCol = globalProbeIdx % u_ProbesPerRow;
	int probeRow = globalProbeIdx / u_ProbesPerRow;
	ivec2 irradianceCoord = ivec2(probeCol * 8  + octTexelX,   probeRow * 8  + octTexelY);
	ivec2 depthCoord      = ivec2(probeCol * 16 + depthTexelX, probeRow * 16 + depthTexelY);

	// Temporal blending with adaptive hysteresis
		vec3  blendedIrradiance = hitRadiance;
		float blendedDepth      = hitDistance;

		// Load history (may be black if first frame for this probe)
		vec4 prevIrradiance = imageLoad(u_IrradianceHistory, irradianceCoord);
		float prevDepth     = imageLoad(u_DepthHistory, depthCoord).r;
		float prevLuma      = dot(prevIrradiance.rgb, vec3(0.333));

		// Skip blending if history is near-black (fresh probe)
		float effectiveHysteresis = (prevLuma > 0.001) ? u_Hysteresis : 0.0;

		blendedIrradiance = mix(hitRadiance, prevIrradiance.rgb, effectiveHysteresis);
		blendedDepth      = mix(hitDistance, prevDepth,        effectiveHysteresis);

	// 在 imageStore 之前添加这个：
if (globalProbeIdx == 0) {
    // 如果是第 0 号探针，直接把深度设为 0.5 (灰色)
    // 如果这样操作后，深度图中有一个点变成了灰色，说明写入逻辑是通的
    imageStore(u_DepthOut, depthCoord, vec4(0.5, 0.0, 0.0, 0.0));
    return; // 临时跳过后面的逻辑
}
	imageStore(u_IrradianceOut, irradianceCoord, vec4(blendedIrradiance, 1.0));
	imageStore(u_DepthOut,      depthCoord,      vec4(blendedDepth, 0.0, 0.0, 0.0));
}
