#pragma once

#include "Platform/RHI/RHIPipeline.h"
#include "Core/Core.h"

// GL implementation of RHIPipeline.
// Bind() applies shader + raster/depth/blend state — all the GL state that
// was previously set implicitly/globally. VAO/VBO/IBO binding remains separate
// (via legacy VertexArray) until Chunk 5 completes the migration.
class T_API GLPipeline : public RHIPipeline
{
public:
    explicit GLPipeline(const PipelineDesc& desc);
    ~GLPipeline() override;

    void Bind() override;
    void Unbind() override;

    Ref<RHIShader> GetShader() const { return m_Shader; }

private:
    Ref<RHIShader> m_Shader;
    PipelineDesc m_Desc;
};

// Helper: convert RHI enums → GL enums
namespace GLPipelineUtil
{
    unsigned int ToGLPrimitive(PrimitiveTopology topo);
    unsigned int ToGLCullFace(CullMode mode);
    unsigned int ToGLCompareOp(CompareOp op);
    unsigned int ToGLBlendFactor(BlendFactor f);
    unsigned int ToGLVertexType(VertexFormat fmt, int& count);
}

// Factory — compile-time binding
inline Ref<RHIPipeline> RHIPipeline::Create(const PipelineDesc& desc)
{
    return CreateRef<GLPipeline>(desc);
}
