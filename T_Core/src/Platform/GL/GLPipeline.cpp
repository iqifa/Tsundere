#include "GLPipeline.h"
#include "GLBuffer.h"
#include "Renderer.h"
#include <GL/glew.h>

// --- GL enum conversion helpers ---

unsigned int GLPipelineUtil::ToGLPrimitive(PrimitiveTopology topo)
{
    switch (topo)
    {
    case PrimitiveTopology::Triangles:     return GL_TRIANGLES;
    case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
    case PrimitiveTopology::Lines:         return GL_LINES;
    default:                               return GL_TRIANGLES;
    }
}

unsigned int GLPipelineUtil::ToGLCullFace(CullMode mode)
{
    switch (mode)
    {
    case CullMode::None:  return GL_NONE;
    case CullMode::Front: return GL_FRONT;
    case CullMode::Back:  return GL_BACK;
    default:              return GL_BACK;
    }
}

unsigned int GLPipelineUtil::ToGLCompareOp(CompareOp op)
{
    switch (op)
    {
    case CompareOp::Never:        return GL_NEVER;
    case CompareOp::Less:         return GL_LESS;
    case CompareOp::Equal:        return GL_EQUAL;
    case CompareOp::LessEqual:    return GL_LEQUAL;
    case CompareOp::Greater:      return GL_GREATER;
    case CompareOp::NotEqual:     return GL_NOTEQUAL;
    case CompareOp::GreaterEqual: return GL_GEQUAL;
    case CompareOp::Always:       return GL_ALWAYS;
    default:                      return GL_LESS;
    }
}

unsigned int GLPipelineUtil::ToGLBlendFactor(BlendFactor f)
{
    switch (f)
    {
    case BlendFactor::Zero:             return GL_ZERO;
    case BlendFactor::One:              return GL_ONE;
    case BlendFactor::SrcAlpha:         return GL_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
    default:                            return GL_ONE;
    }
}

unsigned int GLPipelineUtil::ToGLVertexType(VertexFormat fmt, int& count)
{
    switch (fmt)
    {
    case VertexFormat::Float:      count = 1; return GL_FLOAT;
    case VertexFormat::Float2:     count = 2; return GL_FLOAT;
    case VertexFormat::Float3:     count = 3; return GL_FLOAT;
    case VertexFormat::Float4:     count = 4; return GL_FLOAT;
    case VertexFormat::Int:        count = 1; return GL_INT;
    case VertexFormat::Int2:       count = 2; return GL_INT;
    case VertexFormat::Int3:       count = 3; return GL_INT;
    case VertexFormat::Int4:       count = 4; return GL_INT;
    case VertexFormat::UByte4Norm: count = 4; return GL_UNSIGNED_BYTE;
    default:                       count = 3; return GL_FLOAT;
    }
}

// --- GLPipeline ---

GLPipeline::GLPipeline(const PipelineDesc& desc)
    : m_Shader(desc.shader), m_Desc(desc)
{
    // Create a VAO for this pipeline.
    // Compute pipelines don't need vertex attributes, but a VAO is still
    // required by GL core profile for any draw call.
    GLCall(glGenVertexArrays(1, &m_VAO));
}

GLPipeline::~GLPipeline()
{
    if (m_VAO)
    {
        GLCall(glDeleteVertexArrays(1, &m_VAO));
        m_VAO = 0;
    }
}

void GLPipeline::Bind()
{
    // VAO — must be bound before IBO binding and draw calls
    GLCall(glBindVertexArray(m_VAO));

    // Shader
    if (m_Shader)
        m_Shader->Bind();

    // Depth state
    if (m_Desc.depthTest)
    {
        GLCall(glEnable(GL_DEPTH_TEST));
    }
    else
    {
        GLCall(glDisable(GL_DEPTH_TEST));
    }

    GLCall(glDepthMask(m_Desc.depthWrite ? GL_TRUE : GL_FALSE));
    GLCall(glDepthFunc(GLPipelineUtil::ToGLCompareOp(m_Desc.depthOp)));

    // Cull face
    if (m_Desc.cullMode == CullMode::None)
    {
        GLCall(glDisable(GL_CULL_FACE));
    }
    else
    {
        GLCall(glEnable(GL_CULL_FACE));
        GLCall(glCullFace(GLPipelineUtil::ToGLCullFace(m_Desc.cullMode)));
    }

    // Blend
    if (m_Desc.srcBlend != BlendFactor::One || m_Desc.dstBlend != BlendFactor::Zero)
    {
        GLCall(glEnable(GL_BLEND));
        GLCall(glBlendFunc(
            GLPipelineUtil::ToGLBlendFactor(m_Desc.srcBlend),
            GLPipelineUtil::ToGLBlendFactor(m_Desc.dstBlend)));
    }
    else
    {
        GLCall(glDisable(GL_BLEND));
    }
}

void GLPipeline::Unbind()
{
    GLCall(glBindVertexArray(0));
    glDisable(GL_CULL_FACE);
    if (m_Shader)
        m_Shader->UnBind();
}

void GLPipeline::SetupVertexFormat(Ref<RHIBuffer> vb)
{
    if (!vb || m_Desc.vertexLayout.attributes.empty())
        return;

    auto* glBuf = static_cast<GLBuffer*>(vb.get());
    if (!glBuf) return;

    GLCall(glBindVertexArray(m_VAO));
    GLCall(glBindBuffer(GL_ARRAY_BUFFER, glBuf->GetGLID()));

    for (auto& attr : m_Desc.vertexLayout.attributes)
    {
        GLCall(glEnableVertexAttribArray(attr.location));

        int count = 0;
        unsigned int type = GLPipelineUtil::ToGLVertexType(attr.format, count);

        GLCall(glVertexAttribPointer(
            attr.location,
            count,
            type,
            GL_FALSE,                                             // normalized
            static_cast<GLsizei>(m_Desc.vertexLayout.stride),
            reinterpret_cast<void*>(static_cast<uintptr_t>(attr.offset))));
    }

    GLCall(glBindVertexArray(0));
    GLCall(glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GLPipeline::SetupIndexBuffer(Ref<RHIBuffer> ib)
{
    if (!ib) return;

    auto* glBuf = static_cast<GLBuffer*>(ib.get());
    if (!glBuf) return;

    // IBO binding is stored in the VAO — bind VAO, bind IBO, unbind VAO.
    GLCall(glBindVertexArray(m_VAO));
    GLCall(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuf->GetGLID()));
    GLCall(glBindVertexArray(0));
}
