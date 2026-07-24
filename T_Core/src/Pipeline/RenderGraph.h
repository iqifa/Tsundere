#pragma once
#include <cstdint>
#include <string>
#include<Platform/RenderAPI.h>
#include<Debug/Debug.h>
using ResourceId = uint32_t;
using PassId = uint32_t;
constexpr ResourceId InvalidResourceId = UINT32_MAX;
constexpr PassId InvalidPassId = UINT32_MAX;

class T_API RenderGraph;
class T_API RenderGraphResources;
class T_API RenderGraphPassBuilder;

struct T_API RGTextureHandle
{
	ResourceId id = InvalidResourceId;
};

struct T_API RGBufferHandle
{
	ResourceId id = InvalidResourceId;
};

struct T_API RDGTextureDesc {
	uint32_t width;
	uint32_t height;
	uint32_t mipLevel;
	uint32_t arrayLayers;
	Format format;
	TextureUsage usage;
};

struct T_API RDGBufferDesc
{
	uint32_t size;
	BufferUsage usage;
};
struct T_API RDGFrameBuffer {
	unsigned int fbo = 0;
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
};
#pragma endregion

#pragma region Pass


struct T_API RGPass
{
	std::string name;

	std::vector<RGResourceAccess> reads;
	std::vector<RGResourceAccess> writes;

	std::function<void(RHIContext&, RenderGraphResources&)> execute;

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
private:
	std::vector<RGResource> resources_;
	std::vector<RGPass> passes_;
	std::vector<RDGFrameBuffer> framebuffers_;

	std::vector<std::vector<PassId>> edges_;
	std::vector<PassId> executionOrder_;
	void AllocateTransientResources();

	RHITexture2D* GetTexture(RGTextureHandle handle);



	RHIBuffer* GetBuffer(RGBufferHandle handle);

	unsigned int GetFramebuffer(
		const std::vector<RGTextureHandle>& colors,
		RGTextureHandle depth = {});
};

class T_API RenderGraphResources
{
public:
	RHITexture2D* GetTexture(RGTextureHandle handle) { return graph_.GetTexture(handle); }
	RHIBuffer* GetBuffer(RGBufferHandle handle) { return graph_.GetBuffer(handle); }

	unsigned int GetFramebuffer(
		const std::vector<RGTextureHandle>& colors,
		RGTextureHandle depth = {})
	{
		return graph_.GetFramebuffer(colors, depth);
	}

private:
	friend class RenderGraph;

	explicit RenderGraphResources(RenderGraph& graph)
		: graph_(graph)
	{
	}

	RenderGraph& graph_;
};