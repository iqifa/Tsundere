#pragma once

#include "RenderGraph.h"
#include <Platform/RHI/RHIPipeline.h>

// Helper functions to create pipelines compatible with RenderGraph passes
namespace RenderGraphPipelineHelper
{
    // Extract the backend-neutral rendering signature from a pass. Returns false
    // when an attachment declaration is invalid; an attachment-less pass is a
    // valid empty signature and therefore must not be used as an error sentinel.
    // Call this during pass Setup() after declaring color/depth attachments.
    bool ExtractRenderingSignature(
        const RGPass& pass,
        const RenderGraph& graph,
        RenderingSignature& signature);

    // Create a pipeline descriptor with attachment configuration from a pass
    // Usage in Pass::Setup():
    //   builder.SetColorOutput(0, colorTarget);
    //   builder.SetDepthOutput(depthTarget);
    //
    //   PipelineDesc desc = CreatePipelineDescForPass(passData, shader, vertexLayout);
    //   pipeline = RHIPipeline::Create(desc);
    PipelineDesc CreatePipelineDescForPass(
        const RGPass& pass,
        const RenderGraph& graph,
        Ref<RHIShader> shader,
        const VertexLayout& vertexLayout,
        const PipelineDesc* baseDesc = nullptr  // Optional base to copy other settings from
    );
}
