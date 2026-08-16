#pragma once
#include <cstdint>
#include <string>
#include <array>
#include <optional>
#include<Platform/RenderAPI.h>
#include<Debug/Debug.h>
using ResourceId = uint32_t;
using PassId = uint32_t;
constexpr ResourceId InvalidResourceId = UINT32_MAX;
constexpr PassId InvalidPassId = UINT32_MAX;

class T_API RenderGraph;
class T_API RenderGraphResources;
class T_API RenderGraphPassBuilder;

// Handles carry the generation of the graph that produced them. Reset() bumps
// the generation, so a handle held across a rebuild fails validation instead of
// silently resolving to whatever resource now occupies that index.
struct T_API RGTextureHandle
{
	ResourceId id = InvalidResourceId;
	uint32_t generation = 0;
};

struct T_API RGBufferHandle
{
	ResourceId id = InvalidResourceId;
	uint32_t generation = 0;
};

struct T_API RDGTextureDesc {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t mipLevel = 1;
	uint32_t arrayLayers = 1;
	Format format = Format::RGBA8_UNORM;
	TextureUsage usage = TextureUsage::ColorAttachment;

	// Sampler state — needed because graph-created textures are sampled directly.
	// A shadow map, for example, requires ClampToBorder + white border so that
	// lookups outside the light frustum return "not in shadow".
	FilterMode minFilter = FilterMode::Linear;
	FilterMode magFilter = FilterMode::Linear;
	WrapMode wrapS = WrapMode::ClampToEdge;
	WrapMode wrapT = WrapMode::ClampToEdge;
	std::array<float, 4> borderColor = { 1.0f, 1.0f, 1.0f, 1.0f };
};

struct T_API RDGBufferDesc
{
	uint32_t size = 0;
	BufferUsage usage = BufferUsage::Storage;
};

// One cached framebuffer object, keyed by the attachment handles requested.
// The key is what the caller asked for, not what resolved successfully — a
// partial key would never match again and would leak an FBO every frame.
struct T_API RDGFrameBuffer {
	Ref<RHIFramebuffer> framebuffer;
	std::vector<ResourceId> colors;
	ResourceId depth = InvalidResourceId;
	uint32_t width = 0;
	uint32_t height = 0;
};

#pragma region RDGResource
enum class T_API RGResourceType
{
	Texture,
	Buffer
};

struct T_API RGResource
{
	std::string name;
	RGResourceType type;

	RDGTextureDesc textureDesc;
	RDGBufferDesc bufferDesc;

	bool imported = false;
	bool exported = false;

	RHITexture2D* importedTexture = nullptr;
	RHIBuffer* importedBuffer = nullptr;

	Ref<RHITexture2D>physicalTexture = nullptr;
	Ref<RHIBuffer>physicalBuffer = nullptr;

	// Execution-order positions of first/last use, filled in by Compile() after
	// the topological sort. Declaration order is not execution order, so these
	// are only meaningful once executionOrder_ exists.
	int firstPass = -1;
	int lastPass = -1;
};

enum class T_API RGAccess
{
	ReadSRV,
	WriteUAV,
	RenderTarget,
	DepthRead,
	DepthWrite,
	CopySrc,
	CopyDst,
	Present
};

struct T_API RGResourceAccess
{
	ResourceId resource;
	RGAccess access;
};

struct T_API ResourceDependencyState
{
	PassId lastWriter = InvalidPassId;
	std::vector<PassId> readers;

	// How the last writer wrote. Needed to decide whether a following read
	// requires a barrier at all: a render-target write is already ordered by
	// the GL pipeline, an image/SSBO write is not.
	RGAccess lastWriterAccess = RGAccess::RenderTarget;
};
#pragma endregion

#pragma region Pass

enum class T_API RGLoadOp
{
	Load,       // preserve existing contents
	Clear,      // clear to the attachment's clear value
	DontCare
};

enum class T_API RGStoreOp
{
	Store,
	DontCare
};

struct T_API RGColorAttachment
{
	RGTextureHandle texture;

	uint32_t slot = 0;

	RGLoadOp loadOp = RGLoadOp::Clear;
	RGStoreOp storeOp = RGStoreOp::Store;

	std::array<float, 4> clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
};

struct T_API RGDepthAttachment
{
	RGTextureHandle texture;

	RGLoadOp loadOp = RGLoadOp::Clear;
	RGStoreOp storeOp = RGStoreOp::Store;

	float clearDepth = 1.0f;

	// A depth attachment the pass only tests against, never writes.
	bool readOnly = false;
};

struct T_API RGPass
{
	std::string name;

	std::vector<RGResourceAccess> reads;
	std::vector<RGResourceAccess> writes;

	// Render-target description. Empty colorAttachments + no depthAttachment
	// means this is a compute/copy pass and the graph binds no framebuffer.
	std::vector<RGColorAttachment> colorAttachments;
	std::optional<RGDepthAttachment> depthAttachment;

	// 0 = derive from the first attachment's texture dimensions.
	uint32_t viewportWidth = 0;
	uint32_t viewportHeight = 0;

	std::function<void(RHICommandBuffer&, RenderGraphResources&)> execute;

	// Derived by Compile(), issued by Execute() immediately before this pass
	// runs. Covers only cross-pass hazards the graph can see from declared
	// reads/writes; a hazard entirely inside one pass's lambda (a compute
	// dispatch feeding a draw in the same pass) is invisible here and must
	// still be handled by that pass.
	BarrierFlags barriers = BarrierFlags::None;

	bool culled = false;
};

#pragma endregion

class T_API RenderGraphPassBuilder
{
public:
	RenderGraphPassBuilder(RenderGraph& graph, RGPass& pass)
		: graph_(graph), pass_(pass) {
	}

	void ReadTexture(RGTextureHandle texture, RGAccess access = RGAccess::ReadSRV);
	void WriteTexture(RGTextureHandle texture, RGAccess access = RGAccess::WriteUAV);

	void ReadBuffer(RGBufferHandle buffer, RGAccess access = RGAccess::ReadSRV);
	void WriteBuffer(RGBufferHandle buffer, RGAccess access = RGAccess::WriteUAV);

	// Declares a render target. This IS a write — the builder registers the
	// dependency itself, so callers must not also call WriteTexture() for the
	// same resource. Beyond the dependency, an attachment also carries the slot
	// and load/store ops, which WriteTexture() cannot express.
	void SetColorAttachment(
		uint32_t slot,
		RGTextureHandle texture,
		RGLoadOp loadOp = RGLoadOp::Clear,
		const std::array<float, 4>& clearColor = { 0.0f, 0.0f, 0.0f, 1.0f });

	void SetDepthAttachment(
		RGTextureHandle texture,
		RGLoadOp loadOp = RGLoadOp::Clear,
		float clearDepth = 1.0f,
		bool readOnly = false);

	// Overrides the viewport. By default the graph uses the first attachment's
	// texture size, which is what a full-resolution pass wants.
	void SetViewport(uint32_t width, uint32_t height);

private:
	RenderGraph& graph_;
	RGPass& pass_;
};
class T_API RenderGraph
{
	friend class RenderGraphResources;

public:
	~RenderGraph();
	RGTextureHandle CreateTexture(
		const RDGTextureDesc& desc,
		std::string_view name);

	RGTextureHandle ImportTexture(
		RHITexture2D* texture,
		const RDGTextureDesc& desc,
		std::string_view name);

	void ExportTexture(RGTextureHandle handle);

	RGBufferHandle CreateBuffer(
		const RDGBufferDesc& desc,
		std::string_view name);

	RGBufferHandle ImportBuffer(
		RHIBuffer* buffer,
		const RDGBufferDesc& desc,
		std::string_view name);

	void ExportBuffer(RGBufferHandle handle);



	template<typename SetupFunc, typename ExecuteFunc>
	void AddPass(
		std::string_view name,
		SetupFunc&& setup,
		ExecuteFunc&& execute)
	{
		RGPass pass;
		pass.name = std::string(name);

		RenderGraphPassBuilder builder(*this, pass);
		setup(builder);

		pass.execute = std::forward<ExecuteFunc>(execute);

		passes_.push_back(std::move(pass));
	}


	void Compile();
	void Execute(RHIContext& context);


	void BuildExecutionOrder();

	void AddEdge(PassId from, PassId to);
	void Reset();

	// Reads a resource the graph has finished producing. Only valid for handles
	// passed to ExportTexture/ExportBuffer: an ordinary transient resource has
	// no guaranteed contents once execution ends, and may later be aliased onto
	// another resource's memory.
	RHITexture2D* GetExportedTexture(RGTextureHandle handle);
	RHIBuffer* GetExportedBuffer(RGBufferHandle handle);

	// Current handle generation. Callers holding handles across a Reset() can
	// compare against this to know they need to rebuild.
	uint32_t GetGeneration() const { return generation_; }

private:
	std::vector<RGResource> resources_;
	std::vector<RGPass> passes_;
	std::vector<RDGFrameBuffer> framebuffers_;

	std::vector<std::vector<PassId>> edges_;
	std::vector<PassId> executionOrder_;

	uint32_t generation_ = 1;

	void AllocateTransientResources();

	// Fills in firstPass/lastPass using positions in executionOrder_.
	void ComputeResourceLifetimes();

	// Binds the pass's attachments and drives the render pass around its lambda.
	void ExecutePass(RGPass& pass, RHICommandBuffer& commandBuffer, RenderGraphResources& resources);

	RHITexture2D* GetTexture(RGTextureHandle handle);



	RHIBuffer* GetBuffer(RGBufferHandle handle);

	// Resolves (and caches) a framebuffer over graph-owned textures.
	// Not exposed to pass lambdas: binding render targets is the graph's job,
	// driven by the attachments declared during setup.
	Ref<RHIFramebuffer> GetFramebuffer(
		const std::vector<RGTextureHandle>& colors,
		RGTextureHandle depth = {});
};

// Resource view handed to a pass's execute lambda.
//
// This is deliberately narrower than RenderGraph: a pass may resolve handles it
// declared, and nothing else. It cannot create resources, add passes, or
// recompile mid-execution. It also guarantees handles resolve against the graph
// that is actually executing, rather than whichever graph a lambda captured.
class T_API RenderGraphResources
{
public:
	RHITexture2D* GetTexture(RGTextureHandle handle) { return graph_.GetTexture(handle); }
	RHIBuffer* GetBuffer(RGBufferHandle handle) { return graph_.GetBuffer(handle); }

private:
	friend class RenderGraph;

	explicit RenderGraphResources(RenderGraph& graph)
		: graph_(graph)
	{
	}

	RenderGraph& graph_;
};
