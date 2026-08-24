#include"RenderGraph.h"
#include <algorithm>

RenderGraph::~RenderGraph()
{
    Reset();
}

RGTextureHandle RenderGraph::CreateTexture(
    const RDGTextureDesc& desc,
    std::string_view name) {
    ResourceId id = (ResourceId)resources_.size();
    RGResource resource;
    resource.name = std::string(name);
    resource.type = RGResourceType::Texture;
    resource.textureDesc = desc;

    resources_.push_back(resource);

    return { id, generation_ };
}

RGTextureHandle RenderGraph::ImportTexture(RHITexture2D* texture, const RDGTextureDesc& desc, std::string_view name)
{
    ResourceId id = (ResourceId)resources_.size();

    RGResource resource;
    resource.name = std::string(name);
    resource.type = RGResourceType::Texture;
    resource.textureDesc = desc;
    resource.imported = true;
    resource.importedTexture = texture;

    resources_.push_back(std::move(resource));
    return { id, generation_ };
}

void RenderGraph::ExportTexture(RGTextureHandle handle)
{
    if (handle.id >= resources_.size())
    {
        Error_Core("RenderGraph: invalid exported texture");
        return;
    }

    resources_[handle.id].exported = true;
}

RGBufferHandle RenderGraph::CreateBuffer(const RDGBufferDesc& desc, std::string_view name)
{
    ResourceId id = (ResourceId)resources_.size();
    RGResource resource;
    resource.name = std::string(name);
    resource.type = RGResourceType::Buffer;
    resource.bufferDesc = desc;
    resources_.push_back(resource);

    return{ id, generation_ };
}

RGBufferHandle RenderGraph::ImportBuffer(RHIBuffer* buffer, const RDGBufferDesc& desc, std::string_view name)
{
    ResourceId id = (ResourceId)resources_.size();

    RGResource resource;
    resource.name = std::string(name);
    resource.type = RGResourceType::Buffer;
    resource.bufferDesc = desc;
    resource.imported = true;
    resource.importedBuffer = buffer;

    resources_.push_back(std::move(resource));
    return { id, generation_ };
}

void RenderGraph::ExportBuffer(RGBufferHandle handle)
{
    if (handle.id >= resources_.size())
    {
        Error_Core("RenderGraph: invalid exported Buffer");
        return;
    }

    resources_[handle.id].exported = true;
}

// Which barrier, if any, a dependency needs.
//
// Only writes that land in *incoherent* memory need one. A render-target write
// is already ordered against a later read by the GL pipeline, and a copy
// (glCopyImageSubData) is coherent — neither requires a barrier. An image store
// from a compute shader does, and that is exactly the ShadowRay -> ShadowApply
// case this exists for.
//
// The bit is chosen by how the *reader* touches the resource, matching GL's
// glMemoryBarrier semantics: the bits describe accesses that happen after the
// barrier, not the write that preceded it.
static BarrierFlags DeriveBarrier(
    RGAccess writerAccess,
    RGAccess readerAccess,
    RGResourceType type)
{
    if (writerAccess != RGAccess::WriteUAV)
        return BarrierFlags::None;

    if (type == RGResourceType::Buffer)
        return BarrierFlags::StorageBuffer;

    switch (readerAccess)
    {
    case RGAccess::ReadSRV:      return BarrierFlags::TextureFetch;
    case RGAccess::WriteUAV:     return BarrierFlags::ShaderImage;
    // glCopyImageSubData reads the image as texture memory.
    case RGAccess::CopySrc:
    case RGAccess::CopyDst:      return BarrierFlags::TextureFetch;
    case RGAccess::RenderTarget:
    case RGAccess::DepthRead:
    case RGAccess::DepthWrite:   return BarrierFlags::Framebuffer;
    default:                     return BarrierFlags::TextureFetch;
    }
}

void RenderGraph::Compile()
{

    edges_.clear();
    edges_.resize(passes_.size());

    executionOrder_.clear();


    for (auto& resource : resources_)
    {
        resource.firstPass = -1;
        resource.lastPass = -1;
    }

    std::vector<ResourceDependencyState> dependencyStates(resources_.size());
    for (PassId index = 0; index < passes_.size(); ++index)
    {
        auto& pass = passes_[index];
        pass.barriers = BarrierFlags::None;

        for (auto& [resourceid, access] : pass.reads)
        {
            if (resourceid >= resources_.size())
            {
                Error_Core("[RenderGraph]: invalid resource id:{0} in pass {1}", resourceid, pass.name);
                continue;
            }


            auto& state = dependencyStates[resourceid];

            auto& resource = resources_[resourceid];

            if (state.lastWriter != InvalidPassId)
            {
                AddEdge(state.lastWriter, index);

                // RAW: this pass reads what lastWriter wrote. If that write was
                // an image store, the read needs a barrier before this pass.
                pass.barriers |= DeriveBarrier(
                    state.lastWriterAccess, access, resource.type);
            }

            if (state.lastWriter == InvalidPassId && !resource.imported)
            {
                Error_Core("[RenderGraph]: invalid resource id:{0} in pass {1},None Write Before Pass Read", resourceid, pass.name);
            }


            state.readers.push_back(index);
        }

        for (auto& [resourceid, access] : pass.writes)
        {
            if (resourceid >= resources_.size())
            {
                Error_Core("RenderGraph: invalid resource id in pass {}", pass.name);
                continue;
            }

            auto& state = dependencyStates[resourceid];

            if (state.lastWriter != InvalidPassId)
            {
                AddEdge(state.lastWriter, index);

                // WAW: two image stores to the same resource are not ordered
                // against each other without a barrier.
                pass.barriers |= DeriveBarrier(
                    state.lastWriterAccess, access, resources_[resourceid].type);
            }

            for (auto reader : state.readers)
            {
                AddEdge(reader, index);
            }
            state.readers.clear();

            state.lastWriter = index;
            state.lastWriterAccess = access;
        }
    }

    BuildExecutionOrder();
    ComputeResourceLifetimes();
    AllocateTransientResources();
}

void RenderGraph::Execute(RHIContext& context)
{
    Ref<RHICommandBuffer> commandBuffer = context.GetCommandBuffer();
    if (!commandBuffer)
    {
        Error_Core("RenderGraph: no command buffer available");
        return;
    }

    RenderGraphResources graphResources(*this);

    commandBuffer->Begin();

    for (PassId passId : executionOrder_)
    {
        auto& pass = passes_[passId];
        if (!pass.culled && pass.execute)
            ExecutePass(pass, *commandBuffer, graphResources);
    }

    commandBuffer->End();
    commandBuffer->Submit();
}

void RenderGraph::ExecutePass(RGPass& pass, RHICommandBuffer& commandBuffer, RenderGraphResources& resources)
{
    // Barriers derived in Compile() from this pass reads of resources an
    // earlier pass wrote incoherently (image store / SSBO). Issued here, before
    // the pass runs, because glMemoryBarrier bits describe the accesses that
    // come *after* the barrier. BarrierFlags::None makes this a no-op.
    commandBuffer.ResourceBarrier(pass.barriers);

    const bool hasDepth = pass.depthAttachment.has_value();

    // No attachments at all: a compute or copy pass. Nothing to bind.
    if (pass.colorAttachments.empty() && !hasDepth)
    {
        pass.execute(commandBuffer, resources);
        return;
    }

    // Attachments carry explicit slots, so flatten them into a dense,
    // slot-ordered list before handing them to the framebuffer cache.
    uint32_t maxSlot = 0;
    for (const auto& attachment : pass.colorAttachments)
        maxSlot = (std::max)(maxSlot, attachment.slot);

    std::vector<RGTextureHandle> colorHandles;
    if (!pass.colorAttachments.empty())
        colorHandles.resize(maxSlot + 1);

    for (const auto& attachment : pass.colorAttachments)
        colorHandles[attachment.slot] = attachment.texture;

    for (uint32_t slot = 0; slot < colorHandles.size(); ++slot)
    {
        if (colorHandles[slot].id == InvalidResourceId)
        {
            Error_Core("RenderGraph: pass {0} leaves color slot {1} unassigned", pass.name, slot);
            return;
        }
    }

    RGTextureHandle depthHandle = hasDepth ? pass.depthAttachment->texture : RGTextureHandle{};

    Ref<RHIFramebuffer> framebuffer = GetFramebuffer(colorHandles, depthHandle);
    if (!framebuffer)
        return;

    // Translate the declared load ops into clear directives.
    RenderPassBeginInfo beginInfo;
    beginInfo.colorClears.resize(colorHandles.size());

    for (const auto& attachment : pass.colorAttachments)
    {
        ColorClear& clear = beginInfo.colorClears[attachment.slot];
        clear.enabled = (attachment.loadOp == RGLoadOp::Clear);
        clear.value = attachment.clearColor;
    }

    if (hasDepth)
    {
        beginInfo.clearDepth = (pass.depthAttachment->loadOp == RGLoadOp::Clear);
        beginInfo.depthClearValue = pass.depthAttachment->clearDepth;
    }

    commandBuffer.BeginRenderPass(framebuffer, beginInfo);

    Viewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(
        pass.viewportWidth != 0 ? pass.viewportWidth : framebuffer->GetWidth());
    viewport.height = static_cast<float>(
        pass.viewportHeight != 0 ? pass.viewportHeight : framebuffer->GetHeight());
    commandBuffer.SetViewport(viewport);

    pass.execute(commandBuffer, resources);

    commandBuffer.EndRenderPass();
}

void RenderGraph::BuildExecutionOrder()
{
    executionOrder_.clear();

    std::vector<uint32_t> indegree(passes_.size(), 0);

    for (PassId from = 0; from < edges_.size(); ++from)
    {
        for (PassId to : edges_[from])
        {
            if (to < indegree.size())
                indegree[to]++;
        }
    }

    std::vector<PassId> ready;
    ready.reserve(passes_.size());

    // Seed in declaration order so the resulting order is stable.
    for (PassId i = 0; i < passes_.size(); ++i)
    {
        if (indegree[i] == 0)
            ready.push_back(i);
    }

    size_t cursor = 0;
    while (cursor < ready.size())
    {
        PassId passId = ready[cursor++];
        executionOrder_.push_back(passId);

        for (PassId next : edges_[passId])
        {
            if (--indegree[next] == 0)
                ready.push_back(next);
        }
    }

    if (executionOrder_.size() != passes_.size())
    {
        Error_Core("RenderGraph: dependency cycle detected");
        executionOrder_.clear();

        // Fallback: declaration order, so a cycle degrades instead of dropping passes.
        for (PassId i = 0; i < passes_.size(); ++i)
            executionOrder_.push_back(i);
    }
}

void RenderGraph::ComputeResourceLifetimes()
{
    // Lifetimes must be expressed in execution positions, not declaration
    // indices — the topological sort may reorder passes, and a later aliasing
    // scheme will compare these ranges to decide what can share memory.
    std::vector<int> executionIndexOf(passes_.size(), -1);
    for (size_t executionIndex = 0; executionIndex < executionOrder_.size(); ++executionIndex)
        executionIndexOf[executionOrder_[executionIndex]] = (int)executionIndex;

    for (auto& resource : resources_)
    {
        resource.firstPass = -1;
        resource.lastPass = -1;
    }

    auto touch = [&](ResourceId resourceId, int executionIndex)
    {
        if (resourceId >= resources_.size() || executionIndex < 0)
            return;

        RGResource& resource = resources_[resourceId];

        if (resource.firstPass == -1 || executionIndex < resource.firstPass)
            resource.firstPass = executionIndex;

        if (executionIndex > resource.lastPass)
            resource.lastPass = executionIndex;
    };

    for (PassId passId = 0; passId < passes_.size(); ++passId)
    {
        const int executionIndex = executionIndexOf[passId];
        const RGPass& pass = passes_[passId];

        for (const auto& access : pass.reads)
            touch(access.resource, executionIndex);

        for (const auto& access : pass.writes)
            touch(access.resource, executionIndex);
    }
}

void RenderGraph::AddEdge(PassId from, PassId to)
{
    if (from == to)
        return;

    if (from >= edges_.size() || to >= passes_.size())
    {
        Error_Core("[RenderGraph]: invalid edge {0} -> {1}", from, to);
        return;
    }

    auto& list = edges_[from];
    if (std::find(list.begin(), list.end(), to) == list.end())
        list.push_back(to);
}

void RenderGraph::AllocateTransientResources()
{
    for (auto& resource : resources_)
    {
        if (resource.firstPass == -1 && !resource.exported)
            continue;

        if (resource.imported)
            continue;

        switch (resource.type)
        {

        case RGResourceType::Texture:
        {
            // Named fields, not aggregate initialisation: Texture2DDesc's last
            // member is filePath, and passing the resource name there would make
            // GLTexture2D try to stbi_load("SceneColor").
            Texture2DDesc desc;
            desc.width = resource.textureDesc.width;
            desc.height = resource.textureDesc.height;
            desc.format = resource.textureDesc.format;
            desc.minFilter = resource.textureDesc.minFilter;
            desc.magFilter = resource.textureDesc.magFilter;
            desc.wrapS = resource.textureDesc.wrapS;
            desc.wrapT = resource.textureDesc.wrapT;
            desc.generateMipmaps = resource.textureDesc.mipLevel > 1;
            desc.pixelData = nullptr;
            desc.dataChannels = 4;
            desc.borderColor[0] = resource.textureDesc.borderColor[0];
            desc.borderColor[1] = resource.textureDesc.borderColor[1];
            desc.borderColor[2] = resource.textureDesc.borderColor[2];
            desc.borderColor[3] = resource.textureDesc.borderColor[3];

            resource.physicalTexture = RHITexture2D::Create(desc);
            if (!resource.physicalTexture)
                Error_Core("RenderGraph: failed to create texture {}", resource.name);
        }
		break;
        case RGResourceType::Buffer:
        {
            resource.physicalBuffer = RHIBuffer::Create({
                resource.bufferDesc.size,
                resource.bufferDesc.usage,});
            if(!resource.physicalBuffer)
                Error_Core("RenderGraph: failed to create buffer {}", resource.name);
        }
            break;
        default:
            break;
        }
    }

}

RHITexture2D* RenderGraph::GetTexture(RGTextureHandle handle)
{
    if (handle.id >= resources_.size())
    {
        Error_Core("RenderGraph: invalid texture handle");
        return nullptr;
    }

    if (handle.generation != generation_)
    {
        Error_Core("RenderGraph: stale texture handle (generation {0}, graph is {1})",
            handle.generation, generation_);
        return nullptr;
    }

    RGResource& resource = resources_[handle.id];

    if (resource.type != RGResourceType::Texture)
    {
        Error_Core("RenderGraph: resource {} is not a texture", resource.name);
        return nullptr;
    }

    if (resource.imported)
        return resource.importedTexture;

    return resource.physicalTexture.get();
}

RHIBuffer* RenderGraph::GetBuffer(RGBufferHandle handle)
{
    if (handle.id >= resources_.size())
    {
        Error_Core("RenderGraph: invalid buffer handle");
        return nullptr;
    }

    if (handle.generation != generation_)
    {
        Error_Core("RenderGraph: stale buffer handle (generation {0}, graph is {1})",
            handle.generation, generation_);
        return nullptr;
    }

    RGResource& resource = resources_[handle.id];

    if (resource.type != RGResourceType::Buffer)
    {
        Error_Core("RenderGraph: resource {} is not a buffer", resource.name);
        return nullptr;
    }

    if (resource.imported)
        return resource.importedBuffer;

    return resource.physicalBuffer.get();
}

RHITexture2D* RenderGraph::GetExportedTexture(RGTextureHandle handle)
{
    if (handle.id >= resources_.size())
    {
        Error_Core("RenderGraph: invalid exported texture handle");
        return nullptr;
    }

    if (!resources_[handle.id].exported)
    {
        Error_Core("RenderGraph: texture {} was not exported", resources_[handle.id].name);
        return nullptr;
    }

    return GetTexture(handle);
}

RHIBuffer* RenderGraph::GetExportedBuffer(RGBufferHandle handle)
{
    if (handle.id >= resources_.size())
    {
        Error_Core("RenderGraph: invalid exported buffer handle");
        return nullptr;
    }

    if (!resources_[handle.id].exported)
    {
        Error_Core("RenderGraph: buffer {} was not exported", resources_[handle.id].name);
        return nullptr;
    }

    return GetBuffer(handle);
}

static bool Matches(
    const RDGFrameBuffer& fb,
    const std::vector<RGTextureHandle>& colors,
    RGTextureHandle depth)
{
    if (fb.depth != depth.id)
        return false;

    if (fb.colors.size() != colors.size())
        return false;

    for (size_t i = 0; i < colors.size(); ++i)
    {
        if (fb.colors[i] != colors[i].id)
            return false;
    }

    return true;
}

Ref<RHIFramebuffer> RenderGraph::GetFramebuffer(const std::vector<RGTextureHandle>& colors, RGTextureHandle depth)
{
    for (auto& fb : framebuffers_)
    {
        if (Matches(fb, colors, depth))
            return fb.framebuffer;
    }

    // Resolve every attachment up front. Building a framebuffer from a partial
    // attachment set would also poison the cache key, so that the same request
    // would miss forever and leak an FBO per frame.
    FramebufferViewDesc viewDesc;
    viewDesc.colorAttachments.reserve(colors.size());

    for (size_t i = 0; i < colors.size(); ++i)
    {
        RHITexture2D* texture = GetTexture(colors[i]);
        if (!texture)
        {
            Error_Core("[RenderGraph]: cannot build framebuffer, color {} unresolved", i);
            return nullptr;
        }

        viewDesc.colorAttachments.push_back(texture);

        if (viewDesc.width == 0)
        {
            viewDesc.width = texture->GetWidth();
            viewDesc.height = texture->GetHeight();
        }
    }

    if (depth.id != InvalidResourceId)
    {
        RHITexture2D* depthTexture = GetTexture(depth);
        if (!depthTexture)
        {
            Error_Core("[RenderGraph]: cannot build framebuffer, depth unresolved");
            return nullptr;
        }

        viewDesc.depthAttachment = depthTexture;
        viewDesc.depthFormat = resources_[depth.id].textureDesc.format;

        if (viewDesc.width == 0)
        {
            viewDesc.width = depthTexture->GetWidth();
            viewDesc.height = depthTexture->GetHeight();
        }
    }

    RDGFrameBuffer fb;
    fb.framebuffer = RHIFramebuffer::CreateView(viewDesc);
    if (!fb.framebuffer)
    {
        Error_Core("[RenderGraph]: failed to create framebuffer view");
        return nullptr;
    }

    fb.width = viewDesc.width;
    fb.height = viewDesc.height;
    fb.depth = depth.id;
    for (const auto& color : colors)
        fb.colors.push_back(color.id);

    framebuffers_.push_back(fb);
    return fb.framebuffer;
}

void RenderGraph::Reset()
{
    // Framebuffer views own their FBO names; releasing the refs deletes them.
    // The attachment textures they point at are owned by resources_ below.
    framebuffers_.clear();
    executionOrder_.clear();
    edges_.clear();
    passes_.clear();
    resources_.clear();

    // Invalidate handles issued before this point.
    ++generation_;
}


void RenderGraphPassBuilder::ReadTexture(RGTextureHandle texture, RGAccess access)
{
    pass_.reads.push_back({ texture.id ,access });
}

void RenderGraphPassBuilder::WriteTexture(RGTextureHandle texture, RGAccess access)
{
    // Render targets carry a slot and load/store ops that this call cannot
    // express, and the attachment setters already register the write.
    if (access == RGAccess::RenderTarget || access == RGAccess::DepthWrite)
    {
        Error_Core("RenderGraph: pass {} must declare render targets via "
                   "SetColorAttachment/SetDepthAttachment, not WriteTexture", pass_.name);
        return;
    }

    pass_.writes.push_back({ texture.id,access });
}

void RenderGraphPassBuilder::ReadBuffer(RGBufferHandle buffer, RGAccess access)
{
    pass_.reads.push_back({ buffer.id,access });
}

void RenderGraphPassBuilder::WriteBuffer(RGBufferHandle buffer, RGAccess access)
{
    pass_.writes.push_back({ buffer.id,access });
}

void RenderGraphPassBuilder::SetColorAttachment(
    uint32_t slot,
    RGTextureHandle texture,
    RGLoadOp loadOp,
    const std::array<float, 4>& clearColor)
{
    for (const auto& existing : pass_.colorAttachments)
    {
        if (existing.slot == slot)
        {
            Error_Core("RenderGraph: pass {0} binds color slot {1} twice", pass_.name, slot);
            return;
        }
    }

    RGColorAttachment attachment;
    attachment.texture = texture;
    attachment.slot = slot;
    attachment.loadOp = loadOp;
    attachment.clearColor = clearColor;

    pass_.colorAttachments.push_back(attachment);

    // An attachment is a write. Registering it here is what keeps callers from
    // having to declare the same dependency twice.
    pass_.writes.push_back({ texture.id, RGAccess::RenderTarget });
}

void RenderGraphPassBuilder::SetDepthAttachment(
    RGTextureHandle texture,
    RGLoadOp loadOp,
    float clearDepth,
    bool readOnly)
{
    if (pass_.depthAttachment.has_value())
    {
        Error_Core("RenderGraph: pass {} binds a depth attachment twice", pass_.name);
        return;
    }

    RGDepthAttachment attachment;
    attachment.texture = texture;
    attachment.loadOp = loadOp;
    attachment.clearDepth = clearDepth;
    attachment.readOnly = readOnly;

    pass_.depthAttachment = attachment;

    // Depth-test-only usage does not modify the buffer, so it is a read
    // dependency; only a depth-writing pass registers a write.
    if (readOnly)
        pass_.reads.push_back({ texture.id, RGAccess::DepthRead });
    else
        pass_.writes.push_back({ texture.id, RGAccess::DepthWrite });
}

void RenderGraphPassBuilder::SetViewport(uint32_t width, uint32_t height)
{
    pass_.viewportWidth = width;
    pass_.viewportHeight = height;
}
