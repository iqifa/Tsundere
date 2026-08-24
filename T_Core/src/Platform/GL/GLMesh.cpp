#include "GLMesh.h"
#include "GLPipeline.h"       // GLPipelineUtil::SetupVertexAttributes
#include "ExternalFiles.h"    // GL functions (glGenVertexArrays, glBufferData, …)
#include "Core/Assets/GPUDeletionQueue.h"

GLMesh::GLMesh(const MeshDesc& desc)
    : m_IndexCount(desc.indexCount)
    , m_VertexCount(desc.vertexCount)
{
    if (!desc.vertexData || !desc.indexData)
        return;

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_IBO);

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(desc.vertexCount * desc.layout.stride),
        desc.vertexData, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        (GLsizeiptr)(desc.indexCount * sizeof(unsigned int)),
        desc.indexData, GL_STATIC_DRAW);

    // Vertex attributes are set up from the caller's layout (handles ivec4
    // BoneIDs via glVertexAttribIPointer).
    GLPipelineUtil::SetupVertexAttributes(desc.layout);

    glBindVertexArray(0);
}

GLMesh::~GLMesh()
{
    unsigned vao = m_VAO, vbo = m_VBO, ibo = m_IBO;
    if (vao != 0)
    {
        GPUDeletionQueue::Enqueue([vao, vbo, ibo]() {
            glDeleteVertexArrays(1, &vao);
            glDeleteBuffers(1, &vbo);
            glDeleteBuffers(1, &ibo);
        });
        m_VAO = m_VBO = m_IBO = 0;
    }
}

void GLMesh::Bind() const
{
    glBindVertexArray(m_VAO);
}

void GLMesh::Unbind() const
{
    glBindVertexArray(0);
}

void GLMesh::Draw() const
{
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, m_IndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
