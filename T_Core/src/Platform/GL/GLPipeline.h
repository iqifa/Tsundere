#pragma once

#include "Platform/RHI/RHIPipeline.h"
#include "Core/Core.h"

// GL implementation of RHIPipeline.
// Bind() applies shader + raster/depth/blend state + binds the internal VAO.
// SetupVertexFormat() / SetupIndexBuffer() are one-time init calls that bake
// the VB format and IB reference into the VAO (GL-specific; no-op in VK).
//
// Usage (backend-agnostic):
//   auto pipe = RHIPipeline::Create(desc);
//   pipe->SetupVertexFormat(vb);   // once: set up attrib pointers in VAO (GL), no-op (VK)
//   pipe->SetupIndexBuffer(ib);    // once: store IBO reference in VAO   (GL), no-op (VK)
//   // ... per frame:
//   pipe->Bind();                   // binds VAO + shader + state
//   glDrawElements(...);
class T_API GLPipeline : public RHIPipeline
{
public:
    explicit GLPipeline(const PipelineDesc& desc);
    ~GLPipeline() override;

    void Bind() override;
    void Unbind() override;

    Ref<RHIShader> GetShader() const { return m_Desc.shader; }

    // Vertex layout this pipeline was created with. Needed by GLCommandBuffer
    // to re-specify attribute pointers when a vertex buffer is bound per draw.
    const VertexLayout& GetVertexLayout() const { return m_Desc.vertexLayout; }
    PrimitiveTopology GetTopology() const { return m_Desc.topology; }

    // Bind only this pipeline's VAO, without re-applying shader/state.
    // Used by GLCommandBuffer's per-draw BindVertexBuffer/BindIndexBuffer.
    void BindVertexArray();

    // GL-specific: one-time VAO setup (Vulkan backend: no-op overrides)
    void SetupVertexFormat(Ref<RHIBuffer> vb) override;
    void SetupIndexBuffer(Ref<RHIBuffer> ib) override;

private:
    PipelineDesc m_Desc;
    unsigned int m_VAO = 0;
};

// Helper: convert RHI enums → GL enums
namespace GLPipelineUtil
{
    unsigned int ToGLPrimitive(PrimitiveTopology topo);
    unsigned int ToGLCullFace(CullMode mode);
    unsigned int ToGLCompareOp(CompareOp op);
    unsigned int ToGLBlendFactor(BlendFactor f);
    unsigned int ToGLVertexType(VertexFormat fmt, int& count);

    // Sets up all vertex attributes for a layout (VAO + VBO must already be bound).
    // Integer attributes (Int/Int2/Int3/Int4) use glVertexAttribIPointer.
    void SetupVertexAttributes(const VertexLayout& layout);
}
