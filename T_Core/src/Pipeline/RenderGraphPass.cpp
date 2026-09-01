#include "RenderGraphPass.h"
#include "RenderGraphPipelineHelper.h"
#include <Platform/RHI/RHIPipeline.h>

// ============================================================================
// RenderGraphBuilder 实现
// ============================================================================

RenderGraphBuilder::RenderGraphBuilder(RenderGraph& graph, RGPass& pass, const std::string& passName)
    : m_Graph(graph), m_Pass(pass), m_PassName(passName)
{
}

RGTextureHandle RenderGraphBuilder::ReadTexture(const std::string& name, RGAccess access)
{
    // 先尝试从本地缓存查找（Pass 创建的资源）
    auto localIt = m_LocalTextures.find(name);
    if (localIt != m_LocalTextures.end())
    {
        RGTextureHandle handle = localIt->second;
        m_Pass.reads.push_back({ handle.id, access });
        return handle;
    }

    // 尝试从全局查找
    RGTextureHandle handle = m_Graph.GetTextureByName(name);
    if (handle.id != InvalidResourceId)
    {
        m_Pass.reads.push_back({ handle.id, access });
    }

    return handle;
}

RGBufferHandle RenderGraphBuilder::ReadBuffer(const std::string& name, RGAccess access)
{
    auto localIt = m_LocalBuffers.find(name);
    if (localIt != m_LocalBuffers.end())
    {
        RGBufferHandle handle = localIt->second;
        m_Pass.reads.push_back({ handle.id, access });
        return handle;
    }

    RGBufferHandle handle = m_Graph.GetBufferByName(name);
    if (handle.id != InvalidResourceId)
    {
        m_Pass.reads.push_back({ handle.id, access });
    }

    return handle;
}

RGTextureHandle RenderGraphBuilder::CreateTexture(const std::string& name, const RDGTextureDesc& desc)
{
    // 使用 PassName.ResourceName 作为全局名称
    std::string fullName = m_PassName + "." + name;

    RGTextureHandle handle = m_Graph.CreateTexture(desc, fullName);

    // 缓存到本地映射
    m_LocalTextures[name] = handle;

    return handle;
}

RGBufferHandle RenderGraphBuilder::CreateBuffer(const std::string& name, const RDGBufferDesc& desc)
{
    std::string fullName = m_PassName + "." + name;

    RGBufferHandle handle = m_Graph.CreateBuffer(desc, fullName);

    m_LocalBuffers[name] = handle;

    return handle;
}

RGTextureHandle RenderGraphBuilder::ImportTexture(RHITexture2D* texture, const RDGTextureDesc& desc, const std::string& name)
{
    std::string fullName = m_PassName + "." + name;

    RGTextureHandle handle = m_Graph.ImportTexture(texture, desc, fullName);

    m_LocalTextures[name] = handle;

    return handle;
}

RGBufferHandle RenderGraphBuilder::ImportBuffer(RHIBuffer* buffer, const RDGBufferDesc& desc, const std::string& name)
{
    std::string fullName = m_PassName + "." + name;

    RGBufferHandle handle = m_Graph.ImportBuffer(buffer, desc, fullName);

    m_LocalBuffers[name] = handle;

    return handle;
}

void RenderGraphBuilder::SetColorOutput(
    uint32_t slot,
    RGTextureHandle texture,
    RGLoadOp loadOp,
    const std::array<float, 4>& clearColor)
{
    // 检查是否已经绑定了该 slot
    for (const auto& existing : m_Pass.colorAttachments)
    {
        if (existing.slot == slot)
        {
            Error_Core("RenderGraph: pass {0} binds color slot {1} twice", m_PassName, slot);
            return;
        }
    }

    RGColorAttachment attachment;
    attachment.texture = texture;
    attachment.slot = slot;
    attachment.loadOp = loadOp;
    attachment.clearColor = clearColor;

    m_Pass.colorAttachments.push_back(attachment);

    // 自动注册为 Write 依赖
    m_Pass.writes.push_back({ texture.id, RGAccess::RenderTarget });
}

void RenderGraphBuilder::SetDepthOutput(
    RGTextureHandle texture,
    RGLoadOp loadOp,
    float clearDepth,
    bool readOnly)
{
    if (m_Pass.depthAttachment.has_value())
    {
        Error_Core("RenderGraph: pass {} binds a depth attachment twice", m_PassName);
        return;
    }

    RGDepthAttachment attachment;
    attachment.texture = texture;
    attachment.loadOp = loadOp;
    attachment.clearDepth = clearDepth;
    attachment.readOnly = readOnly;

    m_Pass.depthAttachment = attachment;

    // 注册依赖
    if (readOnly)
        m_Pass.reads.push_back({ texture.id, RGAccess::DepthRead });
    else
        m_Pass.writes.push_back({ texture.id, RGAccess::DepthWrite });
}

void RenderGraphBuilder::Export(RGTextureHandle texture)
{
    m_Graph.ExportTexture(texture);
}

void RenderGraphBuilder::Export(RGBufferHandle buffer)
{
    m_Graph.ExportBuffer(buffer);
}

Ref<RHIPipeline> RenderGraphBuilder::CreatePipeline(
    Ref<RHIShader> shader,
    const VertexLayout& vertexLayout,
    const PipelineDesc* baseDesc)
{
    // 使用 helper 从 pass 提取 attachment 配置
    PipelineDesc desc = RenderGraphPipelineHelper::CreatePipelineDescForPass(
        m_Pass,
        m_Graph,
        shader,
        vertexLayout,
        baseDesc
    );

    if (!desc.shader)
    {
        Error_Core("RenderGraph: pass {} has an invalid pipeline rendering signature", m_Pass.name);
        return nullptr;
    }

    return RHIPipeline::Create(desc);
}

// ============================================================================
// RenderGraphContext 实现
// ============================================================================

RHITexture2D* RenderGraphContext::GetTexture(RGTextureHandle handle)
{
    return m_Resources.GetTexture(handle);
}

RHIBuffer* RenderGraphContext::GetBuffer(RGBufferHandle handle)
{
    return m_Resources.GetBuffer(handle);
}

void RenderGraphContext::BindTexture(uint32_t slot, RGTextureHandle handle)
{
    RHITexture2D* texture = GetTexture(handle);
    if (texture)
    {
        m_Cmd.BindTexture2D(slot, texture->GetNativeID());
    }
}
