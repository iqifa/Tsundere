#pragma once
// Shared prelude for all render passes. Split out of the original
// monolithic RenderPass.h; contents are unchanged.

#include<GLHead.h>
#include<HeadLine.h>
#include<Scene/Scene.h>
#include<Scene/Mesh.h>
#include<Scene/BVHBuilder.h>
#include<Panels/MeshFilePath.h>
#include<Panels/Material.h>
#include<DDGI/DDGI.h>
#include<set>
#include<algorithm>
#include<Debug/Debug.h>
#include<Pipeline/RenderGraph.h>
class Scene;
class  FrameBuffer;

// CPU-side values that graph passes hand to each other.
//
// Textures travel as RGTextureHandle, but some results are plain CPU data — the
// shadow map's light view-projection, for instance, is computed by ShadowMapPass
// and needed by GeometryPass. Held by Ref so pass lambdas can capture it without
// depending on the lifetime of whoever built the graph. Ordering is guaranteed by
// the resource edges: a consumer that reads the producer's texture runs after it.
struct RGFrameData
{
	mat4 ShadowLightViewProj = mat4(1.0f);
	bool HasShadowMap = false;

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


struct  RenderResources
{
	// --- Legacy raw GL IDs (kept for backward compat during RHI migration) ---
	unsigned int SceneColorTexture = 0; // 上一阶段输出的场景颜色
	unsigned int VelocityTexture = 0;   // 上一阶段输出的运动矢量缓存 (Motion Vectors)
	unsigned int DepthTexture = 0;      // 深度图
	unsigned int ShadowMask = 0;
	unsigned int ShadowMapDepth = 0;  // directional-light shadow map depth texture (D32_SFLOAT)
	glm::mat4 ShadowLightViewProj = glm::mat4(1.0f); // light view-projection matrix (set by ShadowMapPass)

	// GBuffer textures (populated by GBufferPass, consumed by DeferredLightingPass)
	unsigned int GBufferPosition = 0;
	unsigned int GBufferNormal = 0;
	unsigned int GBufferAlbedo = 0;
	unsigned int GBufferSpecular = 0;        // 阴影遮罩纹理 (R8)

	// DDGI probe atlas textures
	unsigned int DDGIIrradianceAtlas = 0;
	unsigned int DDGIDepthAtlas = 0;

	// 渲染目标尺寸
	unsigned int SourceFBO = 0;        // 几何 Pass 的主 FBO（用于深度拷贝）

	// --- RHI handles (set by RHI-migrated passes, nullptr until migrated) ---
	// These coexist with the legacy raw IDs during the transition.
	// RHI-migrated passes set BOTH the RHI handle AND the raw ID (via GetNativeID()).
	// Non-migrated passes only set the raw ID, leaving RHI handles as nullptr.
	Ref<RHITexture2D> SceneColorRHI;   // RHI-backed scene color
	Ref<RHITexture2D> VelocityRHI;     // RHI-backed velocity / motion vectors
	Ref<RHITexture2D> DepthRHI;        // RHI-backed depth texture
	Ref<RHITexture2D> ShadowMaskRHI;   // RHI-backed shadow mask (R8)
	Ref<RHIFramebuffer> TargetFBO;     // RHI-backed render target FBO
};

class  RenderPass
{
public:
	virtual ~RenderPass() = default;

	virtual void Init(Ref<FrameBuffer>& m_GBuffer, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) {}

	virtual void Execute(Ref<Scene> scene, RenderResources& resources) = 0;
};

