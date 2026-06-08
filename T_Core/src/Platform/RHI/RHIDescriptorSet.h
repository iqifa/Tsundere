#pragma once

#include "RHITypes.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "Core/Core.h"
#include "HeadLine.h"

// Descriptor set — bundles texture/buffer bindings into one object.
// GL backend: Apply() immediately calls glActiveTexture/glBindTexture/glBindBufferBase.
// VK backend: stores VkDescriptorSet, Apply() records vkCmdBindDescriptorSets.

class T_API RHIDescriptorSet
{
public:
    virtual ~RHIDescriptorSet() = default;

    // Bind a 2D texture to a sampler slot (unit). The unit corresponds to
    // the sampler uniform value set via shader->SetUniform1i("name", unit).
    virtual void BindTexture(uint32_t binding, Ref<RHITexture2D> texture, uint32_t unit) = 0;

    // Bind a cube map to a sampler slot.
    virtual void BindCubeMap(uint32_t binding, Ref<RHITextureCube> cubemap, uint32_t unit) = 0;

    // Bind a storage image for compute shader read/write.
    virtual void BindStorageImage(uint32_t binding, Ref<RHIStorageImage> image, ImageAccess access, uint32_t unit) = 0;

    // Bind a uniform buffer or storage buffer to a binding point.
    virtual void BindUniformBuffer(uint32_t binding, Ref<RHIBuffer> buffer) = 0;
    virtual void BindStorageBuffer(uint32_t binding, Ref<RHIBuffer> buffer) = 0;

    // Apply all bindings now.
    // GL: immediate glActiveTexture + glBindTexture + glBindBufferBase calls.
    // VK: records VkDescriptorSet binding for later vkCmdBindDescriptorSets.
    virtual void Apply(uint32_t slot = 0) = 0;

    // Reset all bindings (called each frame before rebuilding the set)
    virtual void Reset() = 0;

    // Factory
    static Ref<RHIDescriptorSet> Create();
};
