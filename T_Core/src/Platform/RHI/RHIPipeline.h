#pragma once

#include "RHITypes.h"
#include "RHIShader.h"
#include "Core/Core.h"
#include "HeadLine.h"
#include <vector>

class RHIBuffer;
class RHIDescriptorSet;

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

// Describes the render-target interface baked into a graphics pipeline.
//
// OpenGL ignores this when creating a pipeline. Vulkan consumes it either via
// VkPipelineRenderingCreateInfo (dynamic rendering) or as a classic render-pass
// compatibility key. Load/store operations deliberately do not live here: they
// belong to an individual pass execution, not to pipeline compatibility.
struct RenderingSignature
{
    std::vector<Format> colorFormats;
    Format depthFormat = Format::Unknown;
    uint32_t sampleCount = 1;
    uint32_t viewMask = 0;

    bool HasAttachments() const
    {
        return !colorFormats.empty() || depthFormat != Format::Unknown;
    }

    bool operator==(const RenderingSignature& other) const
    {
        return colorFormats == other.colorFormats &&
               depthFormat == other.depthFormat &&
               sampleCount == other.sampleCount &&
               viewMask == other.viewMask;
    }
};

// Kept as an alias while existing backend code migrates to RenderingSignature.
using RenderPassAttachmentConfig = RenderingSignature;

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

    // Descriptor sets whose layouts are part of the pipeline interface.
    // OpenGL ignores this; Vulkan uses their native layouts in VkPipelineLayout.
    std::vector<Ref<RHIDescriptorSet>> descriptorSets;

    // Render-target interface. RDG derives this from the current pass and all
    // graphics backends may use it for validation or pipeline caching.
    RenderingSignature renderingSignature;
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

    // One-time vertex format setup. Call once after pipeline + VB creation.
    // GL: binds VB to internal VAO, sets up glVertexAttribPointer for each attr.
    // VK: no-op (vertex input state is baked into VkPipeline at creation).
    virtual void SetupVertexFormat(Ref<RHIBuffer> vb) { (void)vb; }

    // One-time index buffer setup. Call once after SetupVertexFormat.
    // GL: binds IB to internal VAO (stored as VAO state).
    // VK: no-op (index buffer is bound per-draw via BindIndexBuffer).
    virtual void SetupIndexBuffer(Ref<RHIBuffer> ib) { (void)ib; }

    // Factory — backend selected at compile time
    static Ref<RHIPipeline> Create(const PipelineDesc& desc);
};
