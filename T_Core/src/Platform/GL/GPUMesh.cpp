#include "GPUMesh.h"
#include "ExternalFiles.h"     // GL functions (glGenVertexArrays, glBufferData, …)

// ── 析构：把 glDelete* 推入延迟队列，确保在 GL 主线程执行 ──
GPUMesh::~GPUMesh()
{
    unsigned vao = m_VAO, vbo = m_VBO, ibo = m_IBO;
    if (vao != 0) {
        GPUDeletionQueue::Enqueue([vao, vbo, ibo]() {
            glDeleteVertexArrays(1, &vao);
            glDeleteBuffers(1, &vbo);
            glDeleteBuffers(1, &ibo);
        });
        m_VAO = m_VBO = m_IBO = 0;
    }
}

GPUMesh::GPUMesh(GPUMesh&& o) noexcept
    : m_VAO(o.m_VAO), m_VBO(o.m_VBO), m_IBO(o.m_IBO),
      m_IndexCount(o.m_IndexCount), m_VertexCount(o.m_VertexCount)
{
    o.m_VAO = o.m_VBO = o.m_IBO = 0;
    o.m_IndexCount = o.m_VertexCount = 0;
}

GPUMesh& GPUMesh::operator=(GPUMesh&& o) noexcept
{
    if (this != &o) {
        unsigned vao = m_VAO, vbo = m_VBO, ibo = m_IBO;
        if (vao != 0) {
            GPUDeletionQueue::Enqueue([vao, vbo, ibo]() {
                glDeleteVertexArrays(1, &vao);
                glDeleteBuffers(1, &vbo);
                glDeleteBuffers(1, &ibo);
            });
        }
        m_VAO = o.m_VAO; m_VBO = o.m_VBO; m_IBO = o.m_IBO;
        m_IndexCount  = o.m_IndexCount;
        m_VertexCount = o.m_VertexCount;
        o.m_VAO = o.m_VBO = o.m_IBO = 0;
        o.m_IndexCount = o.m_VertexCount = 0;
    }
    return *this;
}

// ── 工厂方法：从 CPU MeshAsset 创建 GPU 对象 ──
// 顶点布局必须与 Mesh::setupMesh() 完全一致，
// 否则现有 shader（location 0-6）取到的数据会错乱。
Ref<GPUMesh> GPUMesh::CreateFromAsset(const MeshAsset& asset)
{
    if (!asset.IsValid()) return nullptr;

    auto mesh = CreateRef<GPUMesh>();
    mesh->m_IndexCount  = static_cast<uint32_t>(asset.indices.size());
    mesh->m_VertexCount = static_cast<uint32_t>(asset.vertices.size());

    glGenVertexArrays(1, &mesh->m_VAO);
    glGenBuffers(1, &mesh->m_VBO);
    glGenBuffers(1, &mesh->m_IBO);

    glBindVertexArray(mesh->m_VAO);

    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, mesh->m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
        asset.vertices.size() * sizeof(Vertex),
        asset.vertices.data(), GL_STATIC_DRAW);

    // IBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->m_IBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        asset.indices.size() * sizeof(unsigned int),
        asset.indices.data(), GL_STATIC_DRAW);

    // 顶点属性（与 Mesh::setupMesh 对应，location 0-6）
    const GLsizei stride = sizeof(Vertex);

    // loc 0: Position (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
        (void*)offsetof(Vertex, Position));
    // loc 1: Normal (vec3)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
        (void*)offsetof(Vertex, Normal));
    // loc 2: TexCoords (vec2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
        (void*)offsetof(Vertex, TexCoords));
    // loc 3: Tangent (vec3)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride,
        (void*)offsetof(Vertex, Tangent));
    // loc 4: Bitangent (vec3)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride,
        (void*)offsetof(Vertex, Bitangent));
    // loc 5: BoneIDs (ivec4) — 整数属性，用 glVertexAttribIPointer
    glEnableVertexAttribArray(5);
    glVertexAttribIPointer(5, 4, GL_INT, stride,
        (void*)offsetof(Vertex, m_BoneIDs));
    // loc 6: Weights (vec4)
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride,
        (void*)offsetof(Vertex, m_Weights));

    glBindVertexArray(0);
    return mesh;
}

Ref<GPUMesh> GPUMesh::Create(const std::vector<Vertex>& vertices,
                             const std::vector<unsigned int>& indices)
{
    if (vertices.empty() || indices.empty()) return nullptr;

    auto mesh = CreateRef<GPUMesh>();
    mesh->m_IndexCount  = static_cast<uint32_t>(indices.size());
    mesh->m_VertexCount = static_cast<uint32_t>(vertices.size());

    glGenVertexArrays(1, &mesh->m_VAO);
    glGenBuffers(1, &mesh->m_VBO);
    glGenBuffers(1, &mesh->m_IBO);

    glBindVertexArray(mesh->m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, mesh->m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
        vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->m_IBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, Position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, Normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, TexCoords));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, Tangent));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, Bitangent));
    glEnableVertexAttribArray(5);
    glVertexAttribIPointer(5, 4, GL_INT, stride, (void*)offsetof(Vertex, m_BoneIDs));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, m_Weights));

    glBindVertexArray(0);
    return mesh;
}

void GPUMesh::Bind() const
{
    glBindVertexArray(m_VAO);
}

void GPUMesh::UnBind() const
{
    glBindVertexArray(0);
}

void GPUMesh::Draw() const
{
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, m_IndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

size_t GPUMesh::GPUSize() const
{
    return m_VertexCount * sizeof(Vertex) + m_IndexCount * sizeof(unsigned int);
}
