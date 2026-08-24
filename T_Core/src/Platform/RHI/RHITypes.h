#pragma once

// RHI shared types — no backend dependencies
// Used by all RHI interfaces and their GL/Vulkan implementations

#include <cstdint>

enum class Format : uint8_t
{
    Unknown = 0,
    // Color formats
    R8_UNORM,
    RGBA8_UNORM,
    RGBA8_SRGB,
    R16F,
    RG16F,
    RGBA16F,
    RGBA32F,
    // Depth/stencil
    D24_UNORM_S8_UINT,
    D32_SFLOAT,
    D32_SFLOAT_S8_UINT,
    // Integer
    R32_UINT,
    R32_SINT
};

enum class BufferUsage : uint8_t
{
    Vertex,
    Index,
    Uniform,
    Storage,
    Staging
};

enum class TextureUsage : uint8_t
{
    Sampled       = 1 << 0,
    Storage       = 1 << 1,
    ColorAttachment = 1 << 2,
    DepthStencil  = 1 << 3,
    TransferSrc   = 1 << 4,
    TransferDst   = 1 << 5
};
inline TextureUsage operator|(TextureUsage a, TextureUsage b) { return static_cast<TextureUsage>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b)); }
inline bool operator&(TextureUsage a, TextureUsage b) { return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0; }

enum class PrimitiveTopology : uint8_t
{
    Triangles,
    TriangleStrip,
    Lines,
    Points
};

enum class CullMode : uint8_t
{
    None,
    Front,
    Back
};

enum class CompareOp : uint8_t
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class BlendFactor : uint8_t
{
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha
};

enum class ShaderStage : uint8_t
{
    Vertex,
    Fragment,
    Compute,
    Geometry
};

enum class FilterMode : uint8_t
{
    Nearest,
    Linear
};

enum class WrapMode : uint8_t
{
    Repeat,
    ClampToEdge,
    ClampToBorder
};

enum class ImageAccess : uint8_t
{
    ReadOnly,
    WriteOnly,
    ReadWrite
};

// What kind of memory access must be made visible before the next pass reads.
//
// Backend-neutral on purpose: a render graph knows a compute pass wrote an
// image and a later pass samples it, but it must not know the GL enum for that.
// The GL backend maps these onto glMemoryBarrier bits, Vulkan onto pipeline
// barrier stage/access masks.
enum class BarrierFlags : uint32_t
{
    None          = 0,
    ShaderImage   = 1 << 0,   // image load/store writes (glBindImageTexture)
    TextureFetch  = 1 << 1,   // texture() / sampler reads
    StorageBuffer = 1 << 2,   // SSBO reads/writes
    Framebuffer   = 1 << 3    // render target writes read back as texture
};

inline BarrierFlags operator|(BarrierFlags a, BarrierFlags b)
{
    return static_cast<BarrierFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline BarrierFlags& operator|=(BarrierFlags& a, BarrierFlags b)
{
    a = a | b;
    return a;
}
inline bool operator&(BarrierFlags a, BarrierFlags b)
{
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// Vertex attribute formats — used by VertexLayout to describe buffer layout
enum class VertexFormat : uint8_t
{
    Float, Float2, Float3, Float4,
    Int, Int2, Int3, Int4,
    UByte4Norm
};

struct Viewport
{
    float x = 0.0f, y = 0.0f;
    float width = 0.0f, height = 0.0f;
    float minDepth = 0.0f, maxDepth = 1.0f;
};

struct Scissor
{
    int32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
};
