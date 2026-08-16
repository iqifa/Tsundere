#pragma once
// ---------------------------------------------------------------------------
// Vertex.h — 网格顶点布局定义
//
// 独立头文件，打破 Mesh.h ↔ GPUMesh.h ↔ MeshAsset.h 的循环依赖。
// 凡是需要 Vertex 类型的地方直接包含本文件。
// ---------------------------------------------------------------------------
#ifndef VERTEX_H
#define VERTEX_H

#include "ExternalFiles.h"   // glm, GLEW, GLFW, Assimp
#define MAX_BONE_INFLUENCE 4

struct Vertex {
    Vertex() = default;
    Vertex(glm::vec3 position,
           glm::vec3 normal   = glm::vec3(0.0f),
           glm::vec2 texcoords = glm::vec2(0.0f))
        : Position(position), Normal(normal), TexCoords(texcoords) {}

    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
    int       m_BoneIDs[MAX_BONE_INFLUENCE] = {};
    float     m_Weights[MAX_BONE_INFLUENCE] = {};
};

#endif // VERTEX_H
