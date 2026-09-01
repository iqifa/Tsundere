#include "RenderGraphPipelineHelper.h"
#include <algorithm>

namespace
{
    bool IsDepthFormat(Format format)
    {
        return format == Format::D24_UNORM_S8_UINT ||
               format == Format::D32_SFLOAT ||
               format == Format::D32_SFLOAT_S8_UINT;
    }

    bool IsSupportedSampleCount(uint32_t sampleCount)
    {
        return sampleCount == 1 || sampleCount == 2 ||
               sampleCount == 4 || sampleCount == 8;
    }
}

namespace RenderGraphPipelineHelper
{
    bool ExtractRenderingSignature(
        const RGPass& pass,
        const RenderGraph& graph,
        RenderingSignature& signature)
    {
        signature = {};
        uint32_t expectedSampleCount = 1;
        uint32_t expectedWidth = 0;
        uint32_t expectedHeight = 0;
        bool sampleCountSet = false;

        if (!pass.colorAttachments.empty())
        {
            uint32_t maxSlot = 0;
            for (const auto& attachment : pass.colorAttachments)
                maxSlot = (std::max)(maxSlot, attachment.slot);

            signature.colorFormats.resize(maxSlot + 1, Format::Unknown);
            std::vector<bool> assigned(maxSlot + 1, false);

            for (const auto& attachment : pass.colorAttachments)
            {
                if (assigned[attachment.slot])
                {
                    Error_Core("RenderGraph: pass {} declares color slot {} more than once", pass.name, attachment.slot);
                    return false;
                }
                assigned[attachment.slot] = true;

                if (attachment.texture.id == InvalidResourceId ||
                    attachment.texture.id >= graph.resources_.size() ||
                    attachment.texture.generation != graph.generation_)
                {
                    Error_Core("RenderGraph: pass {} has an invalid color attachment handle", pass.name);
                    return false;
                }

                const auto& resource = graph.resources_[attachment.texture.id];
                if (resource.type != RGResourceType::Texture ||
                    resource.textureDesc.format == Format::Unknown ||
                    IsDepthFormat(resource.textureDesc.format) ||
                    !(resource.textureDesc.usage & TextureUsage::ColorAttachment))
                {
                    Error_Core("RenderGraph: pass {} color slot {} is not a color-attachment texture", pass.name, attachment.slot);
                    return false;
                }

                if (resource.textureDesc.width == 0 || resource.textureDesc.height == 0)
                {
                    Error_Core("RenderGraph: pass {} color slot {} has zero dimensions", pass.name, attachment.slot);
                    return false;
                }

                if (!IsSupportedSampleCount(resource.textureDesc.sampleCount))
                {
                    Error_Core("RenderGraph: pass {} color slot {} uses unsupported sample count {}", pass.name, attachment.slot, resource.textureDesc.sampleCount);
                    return false;
                }

                if (sampleCountSet && resource.textureDesc.sampleCount != expectedSampleCount)
                {
                    Error_Core("RenderGraph: pass {} color attachments have mismatched sample counts", pass.name);
                    return false;
                }
                if (sampleCountSet &&
                    (resource.textureDesc.width != expectedWidth ||
                     resource.textureDesc.height != expectedHeight))
                {
                    Error_Core("RenderGraph: pass {} color attachments have mismatched dimensions", pass.name);
                    return false;
                }
                expectedSampleCount = resource.textureDesc.sampleCount;
                expectedWidth = resource.textureDesc.width;
                expectedHeight = resource.textureDesc.height;
                sampleCountSet = true;
                signature.colorFormats[attachment.slot] = resource.textureDesc.format;
            }

            for (bool slotAssigned : assigned)
            {
                if (!slotAssigned)
                {
                    Error_Core("RenderGraph: pass {} leaves a sparse color slot", pass.name);
                    return false;
                }
            }
        }

        if (pass.depthAttachment.has_value())
        {
            const auto& attachment = *pass.depthAttachment;
            if (attachment.texture.id == InvalidResourceId ||
                attachment.texture.id >= graph.resources_.size() ||
                attachment.texture.generation != graph.generation_)
            {
                Error_Core("RenderGraph: pass {} has an invalid depth attachment handle", pass.name);
                return false;
            }

            const auto& resource = graph.resources_[attachment.texture.id];
            if (resource.type != RGResourceType::Texture ||
                !IsDepthFormat(resource.textureDesc.format) ||
                !(resource.textureDesc.usage & TextureUsage::DepthStencil))
            {
                Error_Core("RenderGraph: pass {} depth output is not a depth-stencil texture", pass.name);
                return false;
            }

            if (resource.textureDesc.width == 0 || resource.textureDesc.height == 0)
            {
                Error_Core("RenderGraph: pass {} depth output has zero dimensions", pass.name);
                return false;
            }

            if (!IsSupportedSampleCount(resource.textureDesc.sampleCount))
            {
                Error_Core("RenderGraph: pass {} depth output uses unsupported sample count {}", pass.name, resource.textureDesc.sampleCount);
                return false;
            }

            if (sampleCountSet && resource.textureDesc.sampleCount != expectedSampleCount)
            {
                Error_Core("RenderGraph: pass {} color/depth attachments have mismatched sample counts", pass.name);
                return false;
            }
            if (sampleCountSet &&
                (resource.textureDesc.width != expectedWidth ||
                 resource.textureDesc.height != expectedHeight))
            {
                Error_Core("RenderGraph: pass {} color/depth attachments have mismatched dimensions", pass.name);
                return false;
            }
            expectedSampleCount = resource.textureDesc.sampleCount;
            expectedWidth = resource.textureDesc.width;
            expectedHeight = resource.textureDesc.height;
            sampleCountSet = true;
            signature.depthFormat = resource.textureDesc.format;
        }

        signature.sampleCount = expectedSampleCount;
        return true;
    }

    PipelineDesc CreatePipelineDescForPass(
        const RGPass& pass,
        const RenderGraph& graph,
        Ref<RHIShader> shader,
        const VertexLayout& vertexLayout,
        const PipelineDesc* baseDesc)
    {
        PipelineDesc desc;
        if (baseDesc)
            desc = *baseDesc;

        desc.shader = shader;
        desc.vertexLayout = vertexLayout;
        RenderingSignature signature;
        if (!ExtractRenderingSignature(pass, graph, signature))
        {
            desc.shader.reset();
            return desc;
        }

        desc.renderingSignature = std::move(signature);
        return desc;
    }
}
