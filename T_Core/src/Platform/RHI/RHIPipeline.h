#pragma once

#include "RHITypes.h"
#include "RHIShader.h"
#include "Core/Core.h"
#include "HeadLine.h"
#include <vector>

// Describes one vertex attribute within a vertex buffer layout.
struct VertexAttribute
{
    uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    uint32_t offset = 0;
    uint32_t binding = 0;
};

// Vertex buffer layout — stride + list of attributes.
// Replaces the old VertexBufferLayout with GL-independent types.
struct VertexLayout
{
    std::vector<VertexAttribute> attributes;
    uint32_t stride = 0;
};

// Pipeline state object descriptor.
// Bundles shader + vertex layout + raster/blend/depth state.
// GL backend: creates a VAO from vertexLayout, applies state at Bind().
// VK backend: creates a VkPipeline.
struct PipelineDesc
{
    Ref<RHIShader> shader;
    VertexLayout vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    CullMode cullMode = CullMode::Back;
    CompareOp depthOp = CompareOp::Less;
    bool depthWrite = true;
    bool depthTest = true;
    BlendFactor srcBlend = BlendFactor::One;
    BlendFactor dstBlend = BlendFactor::Zero;
    bool isCompute = false;
};

// Pipeline state object interface.
// Combines shader + vertex layout + render state into one bindable object.
class T_API RHIPipeline
{
public:
    virtual ~RHIPipeline() = default;

    // Bind pipeline state: shader + VAO + raster/depth/blend state
    virtual void Bind() = 0;
    virtual void Unbind() = 0;

    // Factory — backend selected at compile time
    static Ref<RHIPipeline> Create(const PipelineDesc& desc);
};
