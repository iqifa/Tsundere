#pragma once

#include "Platform/RHI/RHIMesh.h"
#include "Platform/RenderAPIConfig.h"
#include "Core/Core.h"

// GL implementation of RHIMesh.
// Owns a VAO + VBO + IBO; destruction is deferred to the GL main thread via
// GPUDeletionQueue, so a shared_ptr released on a worker thread is safe.
class T_API GLMesh : public RHIMesh
{
public:
    explicit GLMesh(const MeshDesc& desc);
    ~GLMesh() override;

    bool IsValid() const override { return m_VAO != 0; }

    void Bind() const override;
    void Unbind() const override;
    void Draw() const override;

    uint32_t GetIndexCount() const override  { return m_IndexCount; }
    uint32_t GetVertexCount() const override { return m_VertexCount; }

private:
    unsigned int m_VAO = 0;
    unsigned int m_VBO = 0;
    unsigned int m_IBO = 0;
    uint32_t m_IndexCount = 0;
    uint32_t m_VertexCount = 0;
};

// Factory — backend selected at compile time
#ifdef RenderAPI_OpenGL
inline Ref<RHIMesh> RHIMesh::Create(const MeshDesc& desc)
{
    return CreateRef<GLMesh>(desc);
}
#endif
