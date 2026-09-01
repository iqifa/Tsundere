#pragma once

#include "Platform/RHI/RHIMesh.h"
#include "Platform/RHI/RHIBuffer.h"
#include "Platform/RHI/RHICommandBuffer.h"
#include "Platform/RHI/RHIPipeline.h"
#include "Core/Core.h"

class T_API VulkanMesh : public RHIMesh
{
public:
    explicit VulkanMesh(const MeshDesc& desc);
    ~VulkanMesh() override = default;

    bool IsValid() const override;
    void Bind() const override {}
    void Unbind() const override {}
    void Draw() const override {}
    void Draw(RHICommandBuffer& commandBuffer) const override;

    uint32_t GetIndexCount() const override { return m_IndexCount; }
    uint32_t GetVertexCount() const override { return m_VertexCount; }

private:
    Ref<RHIBuffer> m_VertexBuffer;
    Ref<RHIBuffer> m_IndexBuffer;
    VertexLayout m_Layout;
    uint32_t m_IndexCount = 0;
    uint32_t m_VertexCount = 0;
};

#ifdef RenderAPI_Vulkan
inline Ref<RHIMesh> RHIMesh::Create(const MeshDesc& desc)
{
    return CreateRef<VulkanMesh>(desc);
}
#endif
