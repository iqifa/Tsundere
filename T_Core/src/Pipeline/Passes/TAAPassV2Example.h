#pragma once
#include "Pipeline/RenderGraphPass.h"
#include "Pipeline/RenderPassRegistry.h"
#include "Pipeline/Passes/PassCommon.h"

// TAAPassV2 - Temporal Anti-Aliasing Pass
// 演示如何使用自动注册系统添加新 Pass
class TAAPassV2 : public RenderGraphPass
{
public:
    TAAPassV2(Ref<Scene> scene, Ref<RGFrameData> frameData)
        : m_Scene(scene), m_FrameData(frameData)
    {
        // 初始化 TAA 相关资源
        // TODO: 创建 Shader, History Buffer 等
    }

    const char* GetName() const override { return "TAA"; }

    void Setup(RenderGraphBuilder& builder) override
    {
        // 1. 读取 Geometry Pass 的输出
        m_SceneColor = builder.ReadTexture("Geometry.SceneColor", RGAccess::ReadSRV);
        m_Velocity = builder.ReadTexture("Geometry.Velocity", RGAccess::ReadSRV);
        m_Depth = builder.ReadTexture("Geometry.Depth", RGAccess::ReadSRV);

        // 2. 创建输出纹理
        RDGTextureDesc outputDesc;
        outputDesc.width = m_Width;
        outputDesc.height = m_Height;
        outputDesc.format = Format::RGBA8_UNORM;
        outputDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

        m_Output = builder.CreateTexture("Output", outputDesc);

        // 3. 声明渲染目标
        builder.SetColorOutput(0, m_Output, RGLoadOp::Clear, { 0.0f, 0.0f, 0.0f, 1.0f });

        // 4. 导出结果（替代 Geometry 的输出）
        builder.Export(m_Output);
    }

    void Execute(RenderGraphContext& context) override
    {
        auto& cmd = context.GetCmd();

        // TODO: 实现 TAA 算法
        // 1. 获取当前帧和历史帧纹理
        // 2. 速度重投影
        // 3. 邻域裁剪
        // 4. 混合当前帧和历史帧

        // 临时实现：直接拷贝输入
        RHITexture2D* sceneColorTex = context.GetTexture(m_SceneColor);
        if (sceneColorTex)
        {
            // 简单的全屏 Blit（实际应用 TAA 算法）
            // cmd.BlitTexture(sceneColorTex, context.GetTexture(m_Output));
        }
    }

    void SetViewportSize(uint32_t width, uint32_t height)
    {
        m_Width = width;
        m_Height = height;
    }

    bool IsEnabled() const override { return m_Enabled; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }

private:
    Ref<Scene> m_Scene;
    Ref<RGFrameData> m_FrameData;

    RGTextureHandle m_SceneColor;
    RGTextureHandle m_Velocity;
    RGTextureHandle m_Depth;
    RGTextureHandle m_Output;

    uint32_t m_Width = 1920;
    uint32_t m_Height = 1080;
    bool m_Enabled = true;
};

// 自动注册：
// - Pass 名称: "TAA"
// - 分类: "PostProcess"
// - 优先级: 60（在 Geometry 之后）
// - 默认禁用（避免影响现有渲染）
REGISTER_RENDER_PASS(TAAPassV2, "TAA", "PostProcess", 60, false)
