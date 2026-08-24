#pragma once

#include "RHITypes.h"
#include "Core/Core.h"
#include "HeadLine.h"
#include <vector>
#include <string>

// --- Texture2D ---

struct Texture2DDesc
{
    uint32_t width = 0, height = 0;
    Format format = Format::RGBA8_UNORM;
    FilterMode minFilter = FilterMode::Linear;
    FilterMode magFilter = FilterMode::Linear;
    WrapMode wrapS = WrapMode::ClampToEdge;
    WrapMode wrapT = WrapMode::ClampToEdge;
    bool generateMipmaps = false;
    const void* pixelData = nullptr;     // raw pixels (stb_image output)
    uint32_t dataChannels = 4;           // 1=R, 3=RGB, 4=RGBA
    std::string filePath;                // optional, for debugging
    // Only uploaded when wrapS/wrapT is ClampToBorder.
    // Default white = "outside the map is fully lit" for shadow maps.
    float borderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
};

class T_API RHITexture2D
{
public:
    virtual ~RHITexture2D() = default;

    virtual void Bind(uint32_t slot) = 0;
    virtual void Unbind() = 0;

    // Bind as a storage image for compute shaders. Only meaningful for
    // storage-capable textures (created with TextureUsage::Storage). Default
    // no-op for plain sampled textures; GL/VK backends implement it.
    virtual void BindAsImage(uint32_t slot, ImageAccess access) { (void)slot; (void)access; }
    virtual void UnbindAsImage(uint32_t slot) { (void)slot; }

    virtual void Resize(uint32_t w, uint32_t h) {}   // default: no-op (static textures)
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    // Returns a backend-specific native handle (GLuint / VkImageView)
    // Used by ImGui::Image() and interop with legacy code
    virtual uintptr_t GetNativeID() const = 0;

    // Source file path (empty for procedurally-created / borrowed textures).
    // Used for material serialization and debugging.
    virtual const std::string& GetPath() const = 0;

    static Ref<RHITexture2D> Create(const Texture2DDesc& desc);
};

// --- TextureCube ---

struct TextureCubeDesc
{
    uint32_t size = 0;                              // face width/height
    Format format = Format::RGBA8_UNORM;
    std::vector<std::string> facePaths;             // 6 paths: +X, -X, +Y, -Y, +Z, -Z
    FilterMode minFilter = FilterMode::Linear;
    FilterMode magFilter = FilterMode::Linear;
};

class T_API RHITextureCube
{
public:
    virtual ~RHITextureCube() = default;

    virtual void Bind(uint32_t slot) = 0;
    virtual uintptr_t GetNativeID() const = 0;

    static Ref<RHITextureCube> Create(const TextureCubeDesc& desc);
};

// --- StorageImage (compute shader read/write) ---

struct StorageImageDesc
{
    uint32_t width = 0, height = 0;
    Format format = Format::RGBA32F;                // typically RGBA32F or R8
};

class T_API RHIStorageImage
{
public:
    virtual ~RHIStorageImage() = default;

    // Bind for compute shader image load/store
    virtual void BindAsImage(uint32_t slot, ImageAccess access) = 0;

    // Unbind a slot previously bound with BindAsImage, so the texture can be
    // sampled as a texture again (GL: glBindImageTexture(slot, 0, ...)).
    virtual void UnbindAsImage(uint32_t slot) = 0;

    // Bind for regular texture sampling
    virtual void BindAsTexture(uint32_t slot) = 0;

    virtual void Resize(uint32_t w, uint32_t h) = 0;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    // Backend-specific native handle (GLuint / VkImageView).
    // Needed because a storage image is also displayed by ImGui and handed to
    // legacy code that still speaks raw GL ids.
    virtual uintptr_t GetNativeID() const = 0;

    static Ref<RHIStorageImage> Create(const StorageImageDesc& desc);
};
