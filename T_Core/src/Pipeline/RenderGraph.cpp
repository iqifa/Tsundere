#include"RenderGraph.h"
#include <algorithm>

RenderGraph::~RenderGraph()
{
    Reset();
}

RGTextureHandle RenderGraph::CreateTexture(
    const RDGTextureDesc& desc,
    std::string_view name) {
    ResourceId id = resources_.size();
    RGResource resource;
    resource.name = std::string(name);
    resource.type = RGResourceType::Texture;
    resource.textureDesc = desc;

    resources_.push_back(resource);

    return { id };
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
    return { id };
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
    ResourceId id = resources_.size();
    RGResource resource;
    resource.name = std::string(name);
    resource.type = RGResourceType::Buffer;
    resource.bufferDesc = desc;
    resources_.push_back(resource);

    return{ id };
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
    return { id };
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
    for (int index = 0; index < passes_.size(); ++index)
    {
        auto& pass = passes_[index];
        for (auto& [resourceid, access] : pass.reads)
        {
            if (resourceid >= resources_.size())
            {
                Error_Core("[RenderGraph]: invalid resource id:{0} in pass {1}", resourceid, pass.name);
                continue;
            }


            auto& state = dependencyStates[resourceid];

            auto& resource = resources_[resourceid];

            resource.lastPass = index;
            if (resource.firstPass == -1)
                resource.firstPass = index;

            if (state.lastWriter != (int)InvalidPassId)
            {
                AddEdge(state.lastWriter, index);
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

            auto& resource = resources_[resourceid];


            resource.lastPass = index;
            if (resource.firstPass == -1)
            {
                resource.firstPass = index;
            }

            if (state.lastWriter != (int)InvalidPassId)
            {
                AddEdge(state.lastWriter, index);
            }

            for (auto reader : state.readers)
            {
                AddEdge(reader, index);
            }
            state.readers.clear();

            state.lastWriter = index;
        }
    }

    BuildExecutionOrder();
    AllocateTransientResources();
}

void RenderGraph::Execute(RHIContext& context)
{
    RenderGraphResources graphResources(*this);
    for (PassId passId : executionOrder_)
    {
        auto& pass = passes_[passId];
        if (!pass.culled && pass.execute)
            pass.execute(context, graphResources);
    }
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

    // 按 pass 添加顺序入队，保证稳定顺序
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

        // fallback：保持添加顺序，方便先跑起来
        for (PassId i = 0; i < passes_.size(); ++i)
            executionOrder_.push_back(i);
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
            resource.physicalTexture = RHITexture2D::Create({
                resource.textureDesc.width,
                resource.textureDesc.height,
                resource.textureDesc.format,
                FilterMode::Linear,
                FilterMode::Linear,
                WrapMode::ClampToEdge,
                WrapMode::ClampToEdge,
                false,
                nullptr,
                4
                });
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
bool Matches(
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
unsigned int RenderGraph::GetFramebuffer(const std::vector<RGTextureHandle>& colors, RGTextureHandle depth)
{


    for (auto& fb : framebuffers_)
    {
        if (Matches(fb, colors, depth))
            return fb.fbo;
    }

    RDGFrameBuffer fb;
    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

    std::vector<GLenum> drawBuffers;

    for (uint32_t i = 0; i < colors.size(); ++i) {
        RHITexture2D* tex = GetTexture(colors[i]);

        if (!tex)
            continue;

        auto glTex = static_cast<unsigned int>(tex->GetNativeID());


        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0 + i,
            GL_TEXTURE_2D,
            glTex,
            0);


        drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
        fb.colors.push_back(colors[i].id);
    }

    if (!drawBuffers.empty())
        glDrawBuffers((GLsizei)drawBuffers.size(), drawBuffers.data());
    else
    {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }


    if (depth.id != InvalidResourceId)
    {
        RHITexture2D* depthTex = GetTexture(depth);
        if (depthTex)
        {
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_2D,
                (GLuint)depthTex->GetNativeID(),
                0);
            fb.depth = depth.id;
        }
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        Error_Core("[RenderGraph]: framebuffer incomplete");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    framebuffers_.push_back(fb);
    return fb.fbo;
}

void RenderGraph::Reset()
{
    for (auto& framebuffer : framebuffers_)
    {
        if (framebuffer.fbo)
            glDeleteFramebuffers(1, &framebuffer.fbo);
    }

    framebuffers_.clear();
    executionOrder_.clear();
    edges_.clear();
    passes_.clear();
    resources_.clear();
}


void RenderGraphPassBuilder::ReadTexture(RGTextureHandle texture, RGAccess access)
{
    pass_.reads.push_back({ texture.id ,access });
}

void RenderGraphPassBuilder::WriteTexture(RGTextureHandle texture, RGAccess access)
{
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
