#pragma once
#ifndef SKYBOX
#define SKYBOX

#include "Platform/RHI/RHIBuffer.h"
#include "Platform/RHI/RHICommandBuffer.h"
#include "Platform/RHI/RHIDescriptorSet.h"
#include "Platform/RHI/RHIPipeline.h"
#include "Platform/RHI/RHIShader.h"
#include "Platform/RHI/RHITexture.h"
#include "Pipeline/RenderGraphPass.h"
#include "HeadLine.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

class RenderGraphBuilder;

struct SkyBox
{
    struct SkyBoxUBO
    {
        glm::mat4 proj;
        glm::mat4 view;
        glm::mat4 prevViewProj;
    };

    std::vector<std::string> texpaths;
    Ref<RHITextureCube> m_Cmp;
    Ref<RHIShader> m_Shader;
    Ref<RHIPipeline> m_Pipeline;
    Ref<RHIBuffer> m_VertexBuffer;
    Ref<RHIBuffer> m_MatrixUBO;
    Ref<RHIDescriptorSet> m_DescriptorSet;

    SkyBox() = default;
    explicit SkyBox(std::vector<std::string> vec)
        : texpaths(std::move(vec))
    {
        static const float skyboxVertices[] = {
            -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f
        };

        m_Cmp = RHITextureCube::Create({ 0, Format::RGBA8_UNORM, texpaths,
                                         FilterMode::Linear, FilterMode::Linear });
        m_Shader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/SkyBox.shader");
        m_VertexBuffer = RHIBuffer::Create({
            static_cast<uint32_t>(sizeof(skyboxVertices)), BufferUsage::Vertex,
            false, skyboxVertices });
        m_MatrixUBO = RHIBuffer::Create({ sizeof(SkyBoxUBO), BufferUsage::Uniform, true, nullptr });
        m_DescriptorSet = RHIDescriptorSet::Create();

        // Declare every structural binding before Vulkan freezes the layout.
        m_DescriptorSet->BindUniformBuffer(0, m_MatrixUBO);
        m_DescriptorSet->BindCubeMap(10, m_Cmp, 10);
    }

    bool InitializePipeline(RenderGraphBuilder& builder)
    {
        if (!m_Shader || !m_VertexBuffer || !m_DescriptorSet)
            return false;

        VertexLayout layout;
        layout.stride = 3 * sizeof(float);
        layout.attributes = { { 0, VertexFormat::Float3, 0, 0 } };

        PipelineDesc desc;
        desc.shader = m_Shader;
        desc.vertexLayout = layout;
        desc.cullMode = CullMode::None;
        desc.depthTest = true;
        desc.depthWrite = false;
        desc.depthOp = CompareOp::LessEqual;
        desc.descriptorSets = { m_DescriptorSet };

        m_Pipeline = builder.CreatePipeline(m_Shader, layout, &desc);
        if (m_Pipeline)
            m_Pipeline->SetupVertexFormat(m_VertexBuffer);
        return m_Pipeline != nullptr;
    }

    bool Draw(RHICommandBuffer& cmd,
              const glm::mat4& proj,
              const glm::mat4& view,
              const glm::mat4& prevViewProj)
    {
        if (!m_Pipeline || !m_VertexBuffer || !m_MatrixUBO ||
            !m_DescriptorSet || !m_Cmp)
            return false;

        SkyBoxUBO matrices{ proj, view, prevViewProj };
        m_MatrixUBO->Upload(&matrices, sizeof(matrices));

        m_DescriptorSet->Reset();
        m_DescriptorSet->BindUniformBuffer(0, m_MatrixUBO);
        m_DescriptorSet->BindCubeMap(10, m_Cmp, 10);
        m_DescriptorSet->Apply(0);

        cmd.BindPipeline(m_Pipeline);
        cmd.BindVertexBuffer(m_VertexBuffer, 0);
        cmd.BindDescriptorSet(m_DescriptorSet, 0);
        cmd.Draw(36, 0);
        return true;
    }

    // Kept for old callers; rendering is now performed by Draw().
    void Bind() {}
};

#endif // SKYBOX
