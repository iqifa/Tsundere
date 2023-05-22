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
    // position
    glm::vec3 Position;
    // normal
    glm::vec3 Normal;
    // texCoords
    glm::vec2 TexCoords;
    // tangent
    glm::vec3 Tangent;
    // bitangent
    glm::vec3 Bitangent;
    //bone indexes which will influence this vertex
    int m_BoneIDs[MAX_BONE_INFLUENCE];
    //weights from each bone
    float m_Weights[MAX_BONE_INFLUENCE];
};
/// <summary>
/// Texture_Struct(id,type,path)
/// </summary>
struct Texture_Struct {
    unsigned int id;
    string type;
    string path;
};

class Mesh {
public:
    // mesh Data
	vector<Vertex>       vertices;
	vector<unsigned int> indices;
	vector<Texture_Struct>      textures;
    unique_ptr<VertexArray>vao;
    unique_ptr<VertexBuffer>vbo;
    unique_ptr<IndexBuffer>ibo;
    // constructor
    Mesh() = default;
    Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture_Struct> textures)
    {
        this->vertices = vertices;
        this->indices = indices;
        this->textures = textures;

        // now that we have all the required data, set the vertex buffers and its attribute pointers.
        setupMesh();
    }

    // render the mesh
    void Draw(Shader& shader)
    {
        // bind appropriate textures
        unsigned int diffuseNr = 1;
        unsigned int specularNr = 1;
        unsigned int normalNr = 1;
        unsigned int heightNr = 1;
        for (unsigned int i = 0; i < textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i); // active proper texture unit before binding
            // retrieve texture number (the N in diffuse_textureN)
            string number;
            string name = textures[i].type;
            if (name == "texture_diffuse")
                number = std::to_string(diffuseNr++);
            else if (name == "texture_specular")
                number = std::to_string(specularNr++); // transfer unsigned int to string
            else if (name == "texture_normal")
                number = std::to_string(normalNr++); // transfer unsigned int to string
            else if (name == "texture_height")
                number = std::to_string(heightNr++); // transfer unsigned int to string

            // now set the sampler to the correct texture unit
            //glUniform1i(glGetUniformLocation(shader.GetID(), (name + number).c_str()), i);
            shader.SetUniform1i((name + number).c_str(), i);
            // and finally bind the texture
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }

        // draw mesh
        Renderer renderer;
        renderer.DrawElement(*vao, *ibo, shader);
        glBindVertexArray(0);
        // always good practice to set everything back to defaults once configured.
        glActiveTexture(GL_TEXTURE0);
    }

private:
    // render data 
    unsigned int VBO, EBO;

    // initializes all the buffer objects/arrays
    void setupMesh()
    {
        //// create buffers/arrays
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
        vao->AddBuffer(*vbo,layout);

        vao->UnBind();
    }
};
#endif