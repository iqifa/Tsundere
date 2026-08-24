#pragma once

// RHIMesh — backend-agnostic GPU mesh (vertex + index buffers + vertex layout).
//
// Replaces the old GL-specific Platform/GL/GPUMesh. The interface carries no
// engine-specific vertex format: callers describe the layout via VertexLayout
// (RHIPipeline.h) and hand raw vertex/index data through MeshDesc.

#include "Platform/RHI/RHIPipeline.h"   // VertexLayout / VertexAttribute
#include "Core/Core.h"
#include "HeadLine.h"
#include <cstdint>

// Describes a mesh upload. Vertex/index data is copied into GPU buffers at
// Create() time, so the CPU-side pointers need only stay valid for the call.
struct MeshDesc
{
    const void*  vertexData = nullptr;
    uint32_t     vertexCount = 0;
    const void*  indexData = nullptr;
    uint32_t     indexCount = 0;         // indices are uint32_t
    VertexLayout layout;
};

class T_API RHIMesh
{
public:
    virtual ~RHIMesh() = default;

    virtual bool IsValid() const = 0;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    // Draws the mesh. Caller must bind the shader/material first.
    virtual void Draw() const = 0;

    virtual uint32_t GetIndexCount() const = 0;
    virtual uint32_t GetVertexCount() const = 0;

    static Ref<RHIMesh> Create(const MeshDesc& desc);
};
