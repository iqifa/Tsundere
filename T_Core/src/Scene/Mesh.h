#pragma once
#ifndef MESH_H
#define MESH_H

#include"GLHead.h"
#include"ExternalFiles.h"
#define MAX_BONE_INFLUENCE 4

struct Vertex {
    Vertex() = default;
    Vertex(vec3 position, vec3 normal = vec3(0.0f, 0.0f, 0.0f), vec2 texcoords = vec2(0.0f, 0.0f)) :
        Position(position),Normal(normal),TexCoords(texcoords){
    }
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
    int m_BoneIDs[MAX_BONE_INFLUENCE];
    float m_Weights[MAX_BONE_INFLUENCE];
};

class Mesh {
public:
    std::vector<Vertex>       vertices;
    std::vector<unsigned int> indices;
    Ptr<VertexArray> vao;
    Ptr<VertexBuffer> vbo;
    Ptr<IndexBuffer> ibo;

    Mesh() = default;

    Mesh(std::vector<Vertex>& vertices, std::vector<unsigned int>& indices)
    {
        this->vertices = vertices;
        this->indices = indices;
        setupMesh();
    }

    void Bind()   const { vao->Bind(); }
    void UnBind() const { vao->UnBind(); }

private:
    void setupMesh()
    {
        vao = CreatePtr<VertexArray>(vertices.size());
        vbo = CreatePtr<VertexBuffer>(&vertices[0].Position.x, vertices.size() * sizeof(Vertex));
        ibo = CreatePtr<IndexBuffer>(&indices[0], indices.size());

        VertexBufferLayout layout;
        layout.Push<vec3>(1);
        layout.Push<vec3>(1);
        layout.Push<vec2>(1);
        layout.Push<vec3>(1);
        layout.Push<vec3>(1);
        layout.Push<int>(4);
        layout.Push<float>(4);
        vao->AddBuffer(*vbo, layout);

        vao->UnBind();
    }
};
#endif
