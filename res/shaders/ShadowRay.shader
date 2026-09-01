#shader compute
#version 430 core

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D u_DepthTex;
layout(binding = 1, r8) uniform writeonly image2D u_ShadowMask;

layout(std140, binding = 0) uniform PerPass_ShadowRay
{
    mat4  u_InvViewProj;     // offset   0
    vec4  u_CameraPos;       // offset  64 (vec3 → vec4)
    vec2  u_Resolution;      // offset  80
    vec4  u_LightDir;        // offset  88 (vec3 → vec4)
    float u_LightDistance;   // offset 104
    // pad 12B  → next vec4 needs 16-byte align at 112
    // (no trailing field; size = 112 bytes)
};

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

layout(std430, binding = 3) buffer TriBuffer
{
	Triangle triangles[];
} u_Triangles;

layout(std430, binding = 4) buffer BVHNodeBuffer
{
	BVHNode nodes[];
} u_BVH;

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

bool rayTriangleIntersect(vec3 ro, vec3 rd, Triangle tri, out float t)
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
	return t > 0.001;
}

bool traceShadowRay(vec3 ro, vec3 rd, float tMax)
{
	vec3 invDir = 1.0 / rd;
	int stack[32];
	int ptr = 0;
	stack[ptr++] = 0;

	while (ptr > 0)
	{
		int idx = stack[--ptr];
		BVHNode node = u_BVH.nodes[idx];

		float tNear, tFar;
		if (!rayAABBIntersect(ro, invDir, node.bboxMin.xyz, node.bboxMax.xyz, tNear, tFar))
			continue;
		if (tNear > tMax)
			continue;

		if (node.bboxMin.w < 0.0)
		{
			int firstTri = int(-node.bboxMin.w) - 1;
			int triCount = int(node.bboxMax.w);

			for (int i = firstTri; i < firstTri + triCount; i++)
			{
				float t;
				if (rayTriangleIntersect(ro, rd, u_Triangles.triangles[i], t) && t < tMax)
					return true;
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

	return false;
}

vec3 getWorldPos(vec2 uv, float depth)
{
	vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	vec4 world = u_InvViewProj * clip;
	return world.xyz / world.w;
}

void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(u_Resolution.x) || pixel.y >= int(u_Resolution.y))
		return;

	vec2 uv = (vec2(pixel) + 0.5) / u_Resolution;
	float depth = texture(u_DepthTex, uv).r;

	if (depth >= 0.9999)
	{
		imageStore(u_ShadowMask, pixel, vec4(1.0));
		return;
	}

	vec3 worldPos = getWorldPos(uv, depth);
	vec3 ro = worldPos + u_LightDir.xyz * 0.01;

	float shadow = traceShadowRay(ro, u_LightDir.xyz, u_LightDistance) ? 0.0 : 1.0;
	imageStore(u_ShadowMask, pixel, vec4(shadow, 0.0, 0.0, 0.0));
}
