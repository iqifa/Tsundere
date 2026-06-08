#pragma once

#include "Platform/RHI/RHIDescriptorSet.h"
#include "Core/Core.h"
#include <vector>

// GL implementation of RHIDescriptorSet.
// Stores bindings and applies them immediately via glActiveTexture/glBindTexture/glBindImageTexture/glBindBufferBase.
// This bridges OpenGL's implicit binding model with the RHI's explicit descriptor set model.
class T_API GLDescriptorSet : public RHIDescriptorSet
{
public:
    GLDescriptorSet() = default;
    ~GLDescriptorSet() override = default;

    void BindTexture(uint32_t binding, Ref<RHITexture2D> texture, uint32_t unit) override;
    void BindCubeMap(uint32_t binding, Ref<RHITextureCube> cubemap, uint32_t unit) override;
    void BindStorageImage(uint32_t binding, Ref<RHIStorageImage> image, ImageAccess access, uint32_t unit) override;
    void BindUniformBuffer(uint32_t binding, Ref<RHIBuffer> buffer) override;
    void BindStorageBuffer(uint32_t binding, Ref<RHIBuffer> buffer) override;

    void Apply(uint32_t slot = 0) override;
    void Reset() override;

private:
    struct TextureBinding {
        Ref<RHITexture2D> texture;
        uint32_t unit;
    };
    struct CubeMapBinding {
        Ref<RHITextureCube> cubemap;
        uint32_t unit;
    };
    struct StorageImageBinding {
        Ref<RHIStorageImage> image;
        uint32_t unit;
        ImageAccess access;
    };
    struct BufferBinding {
        Ref<RHIBuffer> buffer;
        uint32_t slot;
        bool isStorage;
    };

    std::vector<TextureBinding> m_Textures;
    std::vector<CubeMapBinding> m_Cubemaps;
    std::vector<StorageImageBinding> m_StorageImages;
    std::vector<BufferBinding> m_Buffers;
};

// Factory — compile-time binding
inline Ref<RHIDescriptorSet> RHIDescriptorSet::Create()
{
    return CreateRef<GLDescriptorSet>();
}
