#pragma once

#include "GLHead.h"
#include "ExternalFiles.h"

// GPU-compatible triangle (std430 layout, 9 * vec4 = 144 bytes)
struct alignas(16) GPUTriangle
{
	glm::vec4 v0;  // xyz = position, w = padding
	glm::vec4 v1;  // xyz = position, w = padding
	glm::vec4 v2;  // xyz = position, w = padding
	glm::vec4 n0;  // xyz = normal, w = materialIndex
	glm::vec4 n1;  // xyz = normal, w = padding
	glm::vec4 n2;  // xyz = normal, w = padding
	glm::vec4 uv0; // xy = texcoord, zw = padding
	glm::vec4 uv1; // xy = texcoord, zw = padding
	glm::vec4 uv2; // xy = texcoord, zw = padding
};

// GPU BVH node (std430 layout, 2 * vec4 = 32 bytes, cache-line friendly)
struct alignas(16) GPUBVHNode
{
	glm::vec4 bboxMin;  // xyz = AABB min, w = leftChild (or encoded leaf flag)
	glm::vec4 bboxMax;  // xyz = AABB max, w = rightChild (or triCount for leaf)
};

// Leaf encoding: bboxMin.w < 0 means leaf
//   firstTriIndex = int(-bboxMin.w) - 1
//   triCount       = int(bboxMax.w)
// Internal node: bboxMin.w = leftChild, bboxMax.w = rightChild (both >= 0)
