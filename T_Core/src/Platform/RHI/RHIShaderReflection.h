#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Resource layout reflected from compiled SPIR-V (see ShaderCompiler.cpp).
// Offsets and sizes follow the block's declared layout (std140 / std430), so
// C++ structs mirroring a block can be checked against them at load time.

struct ShaderBlockMember
{
    std::string Name;
    std::string Type;
    uint32_t    Offset      = 0;
    uint32_t    Size        = 0;   // 0 for runtime-sized arrays
    uint32_t    ArrayStride = 0;   // 0 when the member is not an array
};

struct ShaderBlock
{
    std::string Name;
    uint32_t    Set     = 0;
    uint32_t    Binding = 0;
    uint32_t    Size    = 0;       // declared size, excluding a trailing runtime array
    std::vector<ShaderBlockMember> Members;
};

struct ShaderImageBinding
{
    std::string Name;
    std::string Type;              // GLSL type name, e.g. "sampler2D", "image2D"
    uint32_t    Set     = 0;
    uint32_t    Binding = 0;
};

struct ShaderReflection
{ 
    std::vector<ShaderBlock>        UniformBlocks;
    std::vector<ShaderBlock>        StorageBlocks;
    std::vector<ShaderImageBinding> SampledImages;
    std::vector<ShaderImageBinding> StorageImages;

    const ShaderBlock* FindUniformBlock(uint32_t binding, uint32_t set = 0) const
    {
        return Find(UniformBlocks, binding, set);
    }

    const ShaderBlock* FindStorageBlock(uint32_t binding, uint32_t set = 0) const
    {
        return Find(StorageBlocks, binding, set);
    }

private:
    static const ShaderBlock* Find(const std::vector<ShaderBlock>& blocks, uint32_t binding, uint32_t set)
    {
        for (const auto& block : blocks)
            if (block.Binding == binding && block.Set == set)
                return &block;
        return nullptr;
    }
};
