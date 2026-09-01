#include "VulkanMesh.h"

#include "Platform/RenderAPIConfig.h"

VulkanMesh::VulkanMesh(const MeshDesc& desc)
    : m_Layout(desc.layout)
    , m_IndexCount(desc.indexCount)
    , m_VertexCount(desc.vertexCount)
{
    if (!desc.vertexData || desc.vertexCount == 0 ||
        !desc.indexData || desc.indexCount == 0 ||
        desc.layout.stride == 0)
    {
        return;
    }

    m_VertexBuffer = RHIBuffer::Create({
        desc.vertexCount * desc.layout.stride,
        BufferUsage::Vertex,
        false,
        desc.vertexData
    });
    m_IndexBuffer = RHIBuffer::Create({
        desc.indexCount * static_cast<uint32_t>(sizeof(uint32_t)),
        BufferUsage::Index,
        false,
        desc.indexData
    });
}

bool VulkanMesh::IsValid() const
{
    return m_VertexBuffer && m_IndexBuffer &&
           m_IndexCount != 0 && m_VertexCount != 0;
}

void VulkanMesh::Draw(RHICommandBuffer& commandBuffer) const
{
    if (!IsValid())
        return;

    commandBuffer.BindVertexBuffer(m_VertexBuffer, 0);
    commandBuffer.BindIndexBuffer(m_IndexBuffer);
    commandBuffer.DrawIndexed(m_IndexCount);
}