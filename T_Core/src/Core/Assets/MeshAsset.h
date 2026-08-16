#pragma once
#include <vector>
#include "Scene/Vertex.h"   // Vertex struct + MAX_BONE_INFLUENCE + ExternalFiles.h

// ---------------------------------------------------------------------------
// MeshAsset — 纯 CPU 侧网格数据
//
// 仅包含顶点和索引数组，没有任何 OpenGL 对象。
// 可在任意线程安全创建、移动、销毁。
//
// 来源：ResourceLoader::ExecuteModelLoad（worker 线程）
// 消费：GPUMesh::CreateFromAsset（主线程创建 VAO/VBO/IBO）
// ---------------------------------------------------------------------------
struct MeshAsset {
    std::vector<Vertex>        vertices;
    std::vector<unsigned int>  indices;

    bool IsValid() const { return !vertices.empty() && !indices.empty(); }

    // 预估 CPU 内存占用（字节）
    size_t CPUSize() const {
        return vertices.size() * sizeof(Vertex) + indices.size() * sizeof(unsigned int);
    }
};
