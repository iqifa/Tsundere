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
#include<Core/Material.h>
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

	struct CameraFrame
	{
		glm::mat4 View = glm::mat4(1.0f);
		glm::mat4 ViewProj = glm::mat4(1.0f);
		glm::mat4 JitteredViewProj = glm::mat4(1.0f);
		glm::mat4 PrevViewProj = glm::mat4(1.0f);
		glm::mat4 PrevJitteredViewProj = glm::mat4(1.0f);
		// Actual screen-space UV displacement of the projection, not the Halton sample.
		glm::vec2 JitterUV = glm::vec2(0.0f);
		glm::vec2 PrevJitterUV = glm::vec2(0.0f);
		bool HistoryValid = false;
	};

	CameraFrame Camera;
	bool CameraJitterEnabled = false;

	void BeginCameraFrame(const glm::mat4& view, const glm::mat4& projection,
		uint32_t width, uint32_t height, const void* cameraIdentity)
	{
		const bool resetHistory = cameraIdentity != m_CameraIdentity ||
			width != m_CameraWidth || height != m_CameraHeight ||
			CameraJitterEnabled != m_PreviousJitterEnabled;
		if (resetHistory)
		{
			m_FrameIndex = 0;
			Camera.HistoryValid = false;
		}

		Camera.View = view;
		Camera.ViewProj = projection * view;
		glm::mat4 jitteredProjection = projection;
		Camera.JitterUV = glm::vec2(0.0f);
		if (CameraJitterEnabled && width != 0 && height != 0)
		{
			const uint32_t index = (m_FrameIndex % 16) + 1;
			const glm::vec2 sample(Halton(index, 2) - 0.5f, Halton(index, 3) - 0.5f);
			jitteredProjection[2][0] += 2.0f * sample.x / static_cast<float>(width);
			jitteredProjection[2][1] += 2.0f * sample.y / static_cast<float>(height);
			// With this perspective projection, [2][xy] shifts NDC by -2*sample/size.
			Camera.JitterUV = -sample / glm::vec2(width, height);
		}
		Camera.JitteredViewProj = jitteredProjection * view;
		if (!Camera.HistoryValid)
		{
			Camera.PrevViewProj = Camera.ViewProj;
			Camera.PrevJitteredViewProj = Camera.JitteredViewProj;
			Camera.PrevJitterUV = Camera.JitterUV;
		}
		m_CameraIdentity = cameraIdentity;
		m_CameraWidth = width;
		m_CameraHeight = height;
		m_PreviousJitterEnabled = CameraJitterEnabled;
	}

	void EndCameraFrame()
	{
		Camera.PrevViewProj = Camera.ViewProj;
		Camera.PrevJitteredViewProj = Camera.JitteredViewProj;
		Camera.PrevJitterUV = Camera.JitterUV;
		Camera.HistoryValid = true;
		++m_FrameIndex;
	}

	void BeginModelFrame(const Scene& scene)
	{
		if (!Camera.HistoryValid || m_ModelScene != &scene || m_ModelRevision != scene.Revision)
			m_PreviousModels.clear();
		m_ModelScene = &scene;
		m_ModelRevision = scene.Revision;
		m_CurrentModels.clear();
		for (auto [id, transform, mesh] : scene.m_Registry.view<const Transform, const MeshRender>().each())
			if (!mesh.ModelPath.empty())
				m_CurrentModels.emplace(id, transform.GetTransform());
	}

	glm::mat4 PreviousModel(entt::entity id, const glm::mat4& current) const
	{
		const auto previous = m_PreviousModels.find(id);
		return previous == m_PreviousModels.end() ? current : previous->second;
	}

	void EndModelFrame() { m_PreviousModels.swap(m_CurrentModels); }

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

private:
	const Scene* m_ModelScene = nullptr;
	uint64_t m_ModelRevision = 0;
	std::unordered_map<entt::entity, glm::mat4> m_PreviousModels;
	std::unordered_map<entt::entity, glm::mat4> m_CurrentModels;

	static float Halton(uint32_t index, uint32_t base)
	{
		float result = 0.0f;
		float fraction = 1.0f;
		while (index != 0)
		{
			fraction /= static_cast<float>(base);
			result += fraction * static_cast<float>(index % base);
			index /= base;
		}
		return result;
	}

	uint32_t m_FrameIndex = 0;
	uint32_t m_CameraWidth = 0;
	uint32_t m_CameraHeight = 0;
	const void* m_CameraIdentity = nullptr;
	bool m_PreviousJitterEnabled = false;
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

