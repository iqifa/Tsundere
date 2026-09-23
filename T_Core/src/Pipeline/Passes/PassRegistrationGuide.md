# Render Pass 自动注册指南

## 概述

使用 `RenderPassRegistry` 系统，新的 Pass 可以自动注册到渲染图，无需在 `VulkanExample` 中手动添加代码。

---

## 方法 1：使用宏自动注册（推荐）

### 步骤 1：定义你的 Pass 类

```cpp
// T_Core/src/Pipeline/Passes/MyCustomPass.h
#pragma once
#include "Pipeline/RenderGraphPass.h"
#include "Pipeline/RenderPassRegistry.h"

class MyCustomPass : public RenderGraphPass
{
public:
    MyCustomPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
        : m_Scene(scene), m_FrameData(frameData)
    {
        // 初始化资源
    }

    const char* GetName() const override { return "MyCustom"; }

    void Setup(RenderGraphBuilder& builder) override
    {
        // 读取输入资源
        m_InputColor = builder.ReadTexture("Geometry.SceneColor");
        
        // 创建输出资源
        RDGTextureDesc desc = { /* ... */ };
        m_OutputColor = builder.CreateTexture("Output", desc);
        
        builder.SetColorOutput(0, m_OutputColor);
        builder.Export(m_OutputColor);
    }

    void Execute(RenderGraphContext& context) override
    {
        auto& cmd = context.GetCmd();
        // 执行渲染逻辑
    }

    void SetViewportSize(uint32_t width, uint32_t height)
    {
        m_Width = width;
        m_Height = height;
    }

private:
    Ref<Scene> m_Scene;
    Ref<RGFrameData> m_FrameData;
    RGTextureHandle m_InputColor;
    RGTextureHandle m_OutputColor;
    uint32_t m_Width = 1920;
    uint32_t m_Height = 1080;
};

// ========================================
// 在文件末尾添加自动注册宏
// ========================================
REGISTER_RENDER_PASS(
    MyCustomPass,       // Pass 类名
    "MyCustom",         // Pass 名称（用于日志和调试）
    "PostProcess",      // 分类（Base/Lighting/PostProcess/Debug 等）
    50,                 // 优先级（数字越小，创建顺序越靠前，但执行顺序由 RDG 决定）
    true                // 默认是否启用
)
```

### 步骤 2：包含 Pass 头文件（一次性）

在任意 **被编译的 .cpp 文件** 中包含你的 Pass 头文件（触发静态初始化）：

```cpp
// SandBox/src/Editor_Panel/VulkanExample.cpp
#include <Pipeline/Passes/MyCustomPass.h>  // 只需 include 一次，自动注册
```

### 步骤 3：完成！无需修改 BuildRenderGraph()

`BuildRenderGraph()` 中的 `CreateDefaultPasses()` 会自动创建所有 `enabledByDefault=true` 的 Pass：

```cpp
void VulkanExampleLayer::BuildRenderGraph(uint32_t width, uint32_t height)
{
    // ...
    
    PassCreationContext ctx;
    ctx.scene = m_Context;
    ctx.frameData = m_GraphFrameData;
    ctx.width = width;
    ctx.height = height;

    // 自动创建所有已注册的 Pass（包括你的 MyCustomPass）
    auto passes = RenderPassRegistry::Get().CreateDefaultPasses(ctx);
    for (auto& pass : passes)
        m_RenderGraph.AddPass(pass);

    m_RenderGraph.Compile();
    // ...
}
```

---

## 方法 2：按需手动创建 Pass（可选）

如果你想控制某些 Pass 的启用条件（比如根据设置动态启用），可以手动创建：

```cpp
void VulkanExampleLayer::BuildRenderGraph(uint32_t width, uint32_t height)
{
    // ...
    
    PassCreationContext ctx = { m_Context, m_GraphFrameData, width, height };

    // 手动创建特定 Pass
    if (m_EnableGeometry)
    {
        auto geometryPass = RenderPassRegistry::Get().CreatePass("Geometry", ctx);
        if (geometryPass)
            m_RenderGraph.AddPass(geometryPass);
    }

    if (m_EnableTAA)
    {
        auto taaPass = RenderPassRegistry::Get().CreatePass("TAA", ctx);
        if (taaPass)
            m_RenderGraph.AddPass(taaPass);
    }

    // 或者：创建某个分类的所有 Pass
    auto postProcessPasses = RenderPassRegistry::Get().CreatePassesByCategory("PostProcess", ctx);
    for (auto& pass : postProcessPasses)
        m_RenderGraph.AddPass(pass);

    m_RenderGraph.Compile();
}
```

---

## 方法 3：运行时动态启用/禁用

Pass 可以重写 `IsEnabled()` 方法，在运行时控制是否执行：

```cpp
class MyCustomPass : public RenderGraphPass
{
public:
    bool IsEnabled() const override { return m_Enabled; }
    
    void SetEnabled(bool enabled) { m_Enabled = enabled; }

private:
    bool m_Enabled = true;
};
```

然后在 UI 中控制：

```cpp
// 在 ImGui 面板中
ImGui::Checkbox("Enable My Custom Pass", &myCustomPass->m_Enabled);
```

---

## 参数说明

### REGISTER_RENDER_PASS 宏参数

```cpp
REGISTER_RENDER_PASS(
    PassClass,          // Pass 类名（不要加引号）
    "PassName",         // Pass 名称字符串（用于日志和按名查找）
    "Category",         // 分类字符串（用于按分类查找）
    Priority,           // 优先级数字（0-1000，数字越小越靠前）
    EnabledByDefault    // bool：默认是否启用
)
```

### 分类建议

- `"Base"` — 基础几何渲染（Geometry, GBuffer）
- `"Lighting"` — 光照相关（DirectionalLight, PointLight, Shadow）
- `"PostProcess"` — 后处理（TAA, Bloom, ToneMapping）
- `"Debug"` — 调试可视化（Wireframe, Normals）

### 优先级建议

- `0-20` — 预处理 Pass（ShadowMap, Prepass）
- `20-50` — 主渲染 Pass（Geometry, GBuffer, Lighting）
- `50-80` — 后处理 Pass（TAA, SSAO, Bloom）
- `80-100` — 最终合成 Pass（ToneMapping, UI）

**注意**：优先级只影响 **创建顺序**，不影响实际执行顺序。执行顺序由 RenderGraph 的依赖分析自动确定。

---

## 简化宏（使用默认参数）

如果你不需要自定义优先级，可以使用简化版：

```cpp
// 自动使用 priority=100, enabledByDefault=true
REGISTER_PASS_SIMPLE(MyCustomPass, "PostProcess")
```

---

## 调试技巧

### 1. 列出所有已注册的 Pass

```cpp
auto names = RenderPassRegistry::Get().GetRegisteredPassNames();
for (auto& name : names)
    Info_Core("Registered Pass: {}", name);
```

### 2. 检查 Pass 是否注册成功

```cpp
auto pass = RenderPassRegistry::Get().CreatePass("MyCustom", ctx);
if (!pass)
    Error_Core("Pass 'MyCustom' not found in registry!");
```

### 3. 查看 Pass 元数据

```cpp
auto& allPasses = RenderPassRegistry::Get().GetAllPasses();
for (auto& [name, meta] : allPasses)
{
    Info_Core("Pass: {}, Category: {}, Priority: {}, Enabled: {}",
        meta.name, meta.category, meta.priority, meta.enabledByDefault);
}
```

---

## 常见问题

### Q: 我添加了 Pass 但没有被自动创建？

**A:** 检查以下几点：
1. 确保 Pass 头文件被某个 .cpp 文件 `#include` 了（触发静态初始化）
2. 检查 `enabledByDefault` 是否为 `true`
3. 检查 `IsEnabled()` 方法是否返回 `true`
4. 在 `BuildRenderGraph()` 中添加日志，查看 `passes.size()`

### Q: Pass 的执行顺序不对？

**A:** 执行顺序由 RenderGraph 自动分析资源依赖决定，不受注册顺序或优先级影响。检查：
1. Pass 的 `Setup()` 中是否正确声明了读写依赖（`ReadTexture` / `CreateTexture`）
2. 资源名称是否匹配（如 `"Geometry.SceneColor"` 必须和创建时的名字一致）

### Q: 如何让 Pass 只在特定条件下创建？

**A:** 有两种方法：
1. 设置 `enabledByDefault=false`，然后手动调用 `CreatePass()` 创建
2. 或者让 Pass 始终创建，但通过 `IsEnabled()` 控制是否执行

---

## 迁移旧代码

### 旧代码（手动添加）：

```cpp
void BuildRenderGraph(uint32_t width, uint32_t height)
{
    auto geometryPass = CreateRef<GeometryPassV2>(m_Context, m_GraphFrameData);
    geometryPass->SetViewportSize(width, height);
    m_RenderGraph.AddPass(geometryPass);

    auto taaPass = CreateRef<TAAPass>(m_Context, m_GraphFrameData);
    taaPass->SetViewportSize(width, height);
    m_RenderGraph.AddPass(taaPass);

    m_RenderGraph.Compile();
}
```

### 新代码（自动注册）：

```cpp
// GeometryPassV2.h 和 TAAPass.h 末尾添加：
REGISTER_RENDER_PASS(GeometryPassV2, "Geometry", "Base", 10, true)
REGISTER_RENDER_PASS(TAAPass, "TAA", "PostProcess", 60, true)

// BuildRenderGraph 简化为：
void BuildRenderGraph(uint32_t width, uint32_t height)
{
    PassCreationContext ctx = { m_Context, m_GraphFrameData, width, height };
    auto passes = RenderPassRegistry::Get().CreateDefaultPasses(ctx);
    for (auto& pass : passes)
        m_RenderGraph.AddPass(pass);

    m_RenderGraph.Compile();
}
```

**减少代码行数：10+ 行 → 4 行**

---

## 总结

✅ 新 Pass 只需 3 步：定义类 → 添加宏 → include 头文件  
✅ 无需修改 `BuildRenderGraph()`（除非需要特殊控制）  
✅ RenderGraph 自动处理执行顺序  
✅ 支持运行时动态启用/禁用  
✅ 易于调试和维护
