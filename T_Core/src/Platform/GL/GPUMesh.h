#pragma once
#include "HeadLine.h"
#include "Core/Assets/MeshAsset.h"      // MeshAsset + Vertex (via Scene/Vertex.h)
#include "Core/Assets/GPUDeletionQueue.h"
#include "Core/Core.h"                  // T_API
#include "Debug/Debug.h"
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// GPUMesh — OpenGL 网格 GPU 对象（VAO + VBO + IBO）
//
// 创建/销毁必须在主 GL 线程。
// 如果 shared_ptr 最后一个引用在非 GL 线程释放，
// 析构函数通过 GPUDeletionQueue 延迟到主线程执行 glDelete*。
// ---------------------------------------------------------------------------
class T_API GPUMesh {
public:
    GPUMesh() = default;
    ~GPUMesh();

    // 不可拷贝
    GPUMesh(const GPUMesh&)            = delete;
    GPUMesh& operator=(const GPUMesh&) = delete;
    // 可移动
    GPUMesh(GPUMesh&& o) noexcept;
    GPUMesh& operator=(GPUMesh&& o) noexcept;

    // ── 工厂：从 CPU 数据创建 GPU 对象（必须在主 GL 线程调用） ──
    static Ref<GPUMesh> CreateFromAsset(const MeshAsset& asset);

    // ── 工厂：直接从 vertex/index vector 创建（避免构造临时 MeshAsset 的拷贝） ──
    static Ref<GPUMesh> Create(const std::vector<Vertex>& vertices,
                               const std::vector<unsigned int>& indices);

    bool IsValid() const { return m_VAO != 0; }

    void Bind()   const;
    void UnBind() const;

    // 绘制（调用前需先 Bind shader 和 material）
    void Draw() const;

    uint32_t GetIndexCount()  const { return m_IndexCount; }
    uint32_t GetVertexCount() const { return m_VertexCount; }

    // 预估显存占用（字节）
    size_t GPUSize() const;

private:
    unsigned int m_VAO         = 0;
    unsigned int m_VBO         = 0;
    unsigned int m_IBO         = 0;
    uint32_t     m_IndexCount  = 0;
    uint32_t     m_VertexCount = 0;
};
