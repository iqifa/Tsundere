#pragma once
// Shared prelude for all render passes. Split out of the original
// monolithic RenderPass.h; contents are unchanged.

#include<Platform/RenderAPI.h>
#include<Debug/Debug.h>
#include<HeadLine.h>
#include<Scene/Scene.h>
#include<Scene/Mesh.h>
#include<Scene/BVHBuilder.h>
#include<Panels/MeshFilePath.h>
#include<Panels/Material.h>
#include<DDGI/DDGI.h>
#include<set>
#include<algorithm>
#include<Pipeline/RenderGraph.h>
#include<Platform/RHI/RHICommandBuffer.h>
class Scene;

// CPU-side values that graph passes hand to each other.
//
// Textures travel as RGTextureHandle, but some results are plain CPU data — the
// shadow map's light view-projection, for instance, is computed by ShadowMapPass
// and needed by GeometryPass. Held by Ref so pass lambdas can capture it without
// depending on the lifetime of whoever built the graph. Ordering is guaranteed by
// the resource edges: a consumer that reads the producer's texture runs after it.
struct RGShadowSlot
{
	glm::mat4 ViewProj = glm::mat4(1.0f);
	glm::vec4 AtlasUV = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f); // xy = scale, zw = offset
};

struct RGFrameData
{
	static constexpr uint32_t MaxShadowSlots = 16;

	bool HasShadowMap = false;
	std::vector<RGShadowSlot> ShadowSlots;
	// Maps light entt::entity -> starting index in ShadowSlots (-1 if no shadow).
	// Directional / Spot consume 1 slot; Point consumes 6 consecutive slots (+X,-X,+Y,-Y,+Z,-Z).
	std::unordered_map<uint32_t, int32_t> LightShadowSlot;

	int32_t FindShadowSlot(entt::entity entity) const
	{
		const auto it = LightShadowSlot.find(static_cast<uint32_t>(entity));
		return it != LightShadowSlot.end() ? it->second : -1;
	}

	// Native id of the shadow map produced this frame, so the editor's debug
	// view can display it without the graph having to export the texture.
	unsigned int ShadowMapDepthID = 0;

	// Outputs of passes that are still legacy-owned (compute + SSBO / image
	// write, stage 7). They are produced outside the graph, before it runs, and
	// read here at execute time rather than captured when the graph is built:
	// DDGI ping-pongs its atlases every frame, so a build-time ID goes stale.
	unsigned int ShadowMaskID = 0;
	unsigned int DDGIIrradianceAtlasID = 0;
	unsigned int DDGIDepthAtlasID = 0;
};

// Per-draw transform. 128 bytes is the Vulkan minimum maxPushConstantsSize.
// viewProj stays in the per-frame camera UBO; MVP is viewProj * model in the shader.
// PushConstants copies these bytes. The shader block must use the same member
// order and offsets; the instance and member names are whatever the shader declares.
struct DrawPushConstants
{
	glm::mat4 model;
	glm::mat4 prevModel;
};
static_assert(sizeof(DrawPushConstants) == 128, "DrawPushConstants must fit in the Vulkan push-constant minimum");

// Per frame, not per draw. jitteredViewProj is what gl_Position uses;
// viewProj is the unjittered matrix motion vectors sample.
struct CameraUBO
{
	glm::mat4 jitteredViewProj;
	glm::mat4 viewProj;
	glm::mat4 prevViewProj;
};

inline void PushDrawModel(RHICommandBuffer& cmd, const glm::mat4& model, const glm::mat4& prevModel)
{
	DrawPushConstants pc;
	pc.model = model;
	pc.prevModel = prevModel;
	cmd.PushConstants(&pc, sizeof(pc));
}

