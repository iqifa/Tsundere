#pragma once
#ifndef MESH_H
#define MESH_H

#include "Scene/Vertex.h"           // Vertex struct + MAX_BONE_INFLUENCE
#include "Platform/GL/GPUMesh.h"    // GPUMesh — GL handle (only safe on main thread)

// ---------------------------------------------------------------------------
// Mesh — 场景网格
//
// CPU 数据（vertices, indices）：长期保留，供 BVH 构建、物理等使用。
// GPU 数据（gpuMesh）：通过 setupMesh() 在主 GL 线程上传，可延迟调用。
//
// 两阶段构造：
//   1. CreatePending(v, i)  — CPU only，可在任意线程构造（异步加载）
//   2. setupMesh()          — 主线程：从 CPU 数据创建 GPUMesh（VAO/VBO/IBO）
//
// IsGPUReady() == false 时禁止渲染该 Mesh。
// ---------------------------------------------------------------------------
class Mesh {
public:
    // CPU 数据（公开，供 BVH/物理访问）
    std::vector<Vertex>        vertices;
    std::vector<unsigned int>  indices;

    // GPU 数据：由 setupMesh() 创建，析构时通过 GPUDeletionQueue 安全释放
    Ref<GPUMesh> gpuMesh;

    Mesh() = default;

    // 同步构造：立即上传 GPU（仅在主 GL 线程调用）
    Mesh(std::vector<Vertex>& verts, std::vector<unsigned int>& idx)
    {
        vertices = verts;
        indices  = idx;
        setupMesh();
    }

    // ── 异步加载用：CPU only，不调用任何 GL ──
    // 返回一个 gpuMesh == nullptr 的 Mesh（IsGPUReady() == false）。
    // 后续在主线程调用 setupMesh() 完成 GPU 上传。
    static Mesh CreatePending(std::vector<Vertex>& verts, std::vector<unsigned int>& idx)
    {
        Mesh m;
        m.vertices = std::move(verts);
        m.indices  = std::move(idx);
        return m;
    }

    // ── 从 MeshAsset 构造（仅移动 CPU 数据，不上传 GPU） ──
    static Mesh CreateFromAsset(MeshAsset&& asset)
    {
        Mesh m;
        m.vertices = std::move(asset.vertices);
        m.indices  = std::move(asset.indices);
        return m;
    }

    // GPU 上传（主 GL 线程）
    void setupMesh()
    {
        if (vertices.empty()) return;
        gpuMesh = GPUMesh::Create(vertices, indices);
    }

    bool IsGPUReady() const { return gpuMesh != nullptr; }

    void Bind()   const { if (gpuMesh) gpuMesh->Bind(); }
    void UnBind() const { if (gpuMesh) gpuMesh->UnBind(); }
};

#endif // MESH_H
