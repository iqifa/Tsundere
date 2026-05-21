# 完整路径追踪 实现文档

## 概述

在光线追踪阴影基础设施之上，实现完整的 GPU Compute Shader 路径追踪。当启用时，路径追踪完全替代光栅化管线（GeometryPass → ShadowPass → ShadowApplyPass → TAAPass），提供基于物理的全局光照效果，包括多次弹射漫反射、天光环境贴图采样和渐进式累积降噪。

与光栅化管线共享 BVH 加速结构，避免重复构建。

## 架构

```
Mode A — 光栅化 (现有, 默认):
  GeometryPass → ShadowPass → ShadowApplyPass → TAAPass → Display

Mode B — 路径追踪 (新增, 运行时切换):
  PathTracePass (Compute Shader, 渐进式累积) → Display
```

### 数据流

```
PathTracePass::Execute()
  ├── 输入:
  │     ├── AccumTex[Current]  (image2D, readonly)  — 上一帧累积结果
  │     ├── BVH SSBO           (Triangle + BVHNode)  — 复用 ShadowPass 构建
  │     ├── Material SSBO      (GPUMaterial[1])      — 默认灰色材质
  │     └── SkyBox             (samplerCube)         — 环境光 (Miss Shader)
  │
  ├── Dispatch: 8×8 workgroup, (W+7)/8 × (H+7)/8 组
  │     ├── 1. 像素 jitter + NDC → 世界空间射线生成
  │     ├── 2. 栈式 BVH 遍历 (Slab 测试 + Möller-Trumbore)
  │     ├── 3. 路径追踪循环 (最多 4 次弹射)
  │     │     ├── Hit: Lambertian 漫反射 + 余弦加权半球采样
  │     │     ├── Miss: 采样 SkyBox 天空盒
  │     │     └── Russian Roulette (弹射 >2 后启用)
  │     ├── 4. 萤火虫钳制 (max 10.0)
  │     ├── 5. 渐进式累积: mix(accum, new, 1/N)
  │     └── 6. Reinhard 色调映射 + 写入 AccumTex[Next]
  │
  └── 输出:
        resources.SceneColorTexture = AccumTex[Next]->GetID()
```

## 新增文件 (2)

| 文件 | 用途 |
|------|------|
| `T_Core/src/Platform/GL/ImageTexture.h` | 计算着色器 image load/store 纹理的轻量封装 |
| `res/shaders/PathTrace.shader` | GLSL 430 完整路径追踪 Compute Shader |

## 修改文件 (5)

| 文件 | 变更内容 |
|------|----------|
| `T_Core/src/GLHead.h` | 注册 `ImageTexture.h` |
| `T_Core/src/Scene/BVH.h` | 新增 `GPUMaterial` 结构体 (std430 布局, 32 bytes) |
| `T_Core/src/Pipeline/RenderPass.h` | 新增 `PathTracePass` 类; `ShadowPass` 暴露 `GetBVHBuilder()` |
| `SandBox/src/ExampleLayer.h` | 新增 `pathTracePass` 成员, `m_PathTraceMode` 模式切换标志 |
| `SandBox/src/ExampleLayer.cpp` | 构造初始化, OnUpdate 管线分支, UI 控件, 缩放处理 |

## 技术细节

### ImageTexture (ImageTexture.h)

轻量级 compute shader 纹理包装器, 不做 stbi 加载, 仅封装 GL 纹理操作:

- **`Bind(unit, access)`**: 调用 `glBindImageTexture(unit, id, 0, GL_FALSE, 0, access, internalFormat)`, 将纹理绑定为 image2D 供 compute shader 读写
- **`BindAsTexture(slot)`**: 调用 `glActiveTexture(GL_TEXTURE0 + slot)` + `glBindTexture(GL_TEXTURE_2D, id)`, 将纹理绑定为 sampler2D 供 fragment shader 采样
- **`Resize(w, h)`**: 删除旧纹理, 重新创建, 用于视口缩放
- **`Create(w, h, fmt)`**: 静态工厂, 返回 `Ref<ImageTexture>`, 默认 `GL_RGBA32F` (HDR 累积需要)

### Compute Shader (PathTrace.shader)

#### Binding 布局

| Binding | 类型 | 内容 | 说明 |
|---------|------|------|------|
| 0 | image2D (readonly, RGBA32F) | 上一帧累积结果 | 第一帧 (sampleIndex=0) 跳过读取 |
| 1 | image2D (writeonly, RGBA32F) | 当前帧输出 | ping-pong 写目标 |
| 3 | SSBO (std430) | Triangle[] | GPU 三角形数据, 复用 ShadowPass BVH |
| 4 | SSBO (std430) | BVHNode[] | GPU BVH 节点, 复用 ShadowPass BVH |
| 5 | SSBO (std430) | Material[] | 材质数组 (albedo + emission) |
| 6 | samplerCube | 天空盒 | Miss Shader 环境光照 |

#### 核心算法

**射线生成**: 像素坐标 (+ sub-pixel jitter) → NDC → clip space → view space → world space, 通过 `u_InvProj` 和 `u_InvView` 逆矩阵变换。

**BVH 遍历** (closest-hit, 复用 ShadowRay.shader 逻辑):
- 32 入口显式栈
- Slab 测试 (ray-AABB intersection) 剔除不命中的包围盒
- Möller-Trumbore 三角形相交, 重心坐标法线插值
- 叶子节点编码: `bboxMin.w < 0` 表示叶子, `firstTri = -w - 1`, `triCount = bboxMax.w`
- 内部节点: `leftChild = bboxMin.w`, `rightChild = bboxMax.w`

**路径追踪循环** (最多 4 次弹射):
1. **命中**: 取材质 albedo, 累加 emission; 余弦加权半球采样生成下一个射线方向; `throughput *= albedo`
2. **未命中**: `throughput * skyboxColor` 累加到 radiance, 循环终止
3. **Russian Roulette**: 弹射 >2 后, 以 `max(throughput)` 为概率决定是否继续; 继续时 `throughput /= p` 保持无偏

**RNG**: PCG Hash, 种子 = `(pixelIndex XOR uint(frameSeed * 2654435761))`, 每像素独立, 帧间变化。

**渐进式累积**: `averaged = mix(accumIn.rgb, newColor, 1.0 / (sampleIndex + 1.0))`, 实现无偏的移动平均。

**后处理**: 萤火虫钳制 `min(color, 10.0)` → Reinhard 色调映射 `color / (color + 1.0)`。

### GPUMaterial (BVH.h)

```cpp
struct alignas(16) GPUMaterial {
    glm::vec4 albedo;   // rgb = 基础色, a = 粗糙度
    glm::vec4 emission; // rgb = 自发光, a = 金属度
};
```

std430 布局, 每个材质 32 bytes。三角形通过 `GPUTriangle::n0.w` (materialIndex) 索引材质数组。当前阶段仅上传一个默认灰色材质 (albedo=0.8, roughness=0.5), 所有三角形共享。

### PathTracePass (RenderPass.h)

**Init**: 创建 compute shader (`PathTrace.shader`), 两个 `ImageTexture(RGBA32F)` 用于 ping-pong 累积, 上传默认材质 SSBO。

**Execute**:
- 相机移动检测: `distance(camPos, lastCamPos) > 0.01` 或视口尺寸变化时重置累积 (`m_SampleCount = 0`)
- Ping-pong: 绑定 `AccumTex[currentIdx]` 为只读 (binding 0), `AccumTex[1-currentIdx]` 为只写 (binding 1)
- 绑定 BVH SSBO (slots 3,4), Material SSBO (slot 5), SkyBox cubemap (slot 6)
- 设置 camera/uniform 参数, `DispatchCompute`, `glMemoryBarrier`
- 交换 ping-pong idx, `m_SampleCount++`, `m_FrameIdx++`
- 输出累积结果纹理 ID 到 `resources.SceneColorTexture`

**OnResize**: 两个 ImageTexture 同时 Resize, 累积重置。

**共享 BVH**: `SetBVHBuilder()` 接收 ShadowPass 已构建的 BVHBuilder 引用, 避免重复 CPU 构建。

### 管线集成 (ExampleLayer)

**构造函数**:
```cpp
pathTracePass = CreateRef<PathTracePass>();
pathTracePass->Init(framebuffer);
pathTracePass->SetBVHBuilder(shadowPass->GetBVHBuilder());
```

**OnUpdate 分支**:
- 路径追踪模式: `framebuffer->Bind()` → Clear → 相机输入 → `pathTracePass->Execute()` → `UnBind()`。跳过 GeometryPass/ShadowPass/ShadowApplyPass/TAAPass。
- 光栅化模式: 现有管线完整保留在 else 分支。

**UI 控件** (State 面板):
- "Path Trace?" 复选框切换模式
- 显示当前累积样本数 (`GetSampleCount()`)
- "Reset Accum" 按钮手动重置累积

**缩放处理**: 视口尺寸变化时调用 `pathTracePass->OnResize(w, h)`。

### BVHBuilder 共享

`ShadowPass` 新增 getter:
```cpp
Ref<BVHBuilder> GetBVHBuilder() const { return m_BVHBuilder; }
```

PathTracePass 通过 `SetBVHBuilder()` 接收引用, 在 Execute 中直接使用:
```cpp
m_BVHBuilder->GetTriangleBuffer()->BindToSlot(3);
m_BVHBuilder->GetBVHNodeBuffer()->BindToSlot(4);
```

## 关键设计决策

1. **只读累积**: 第一帧 (`sampleIndex == 0`) 跳过对 binding 0 的 `imageLoad`, 直接写入 binding 1, 因为未初始化的 RGBA32F 纹理被 imageLoad 读取会产生未定义行为。

2. **相机重置**: 移动超过 0.01 单位或视口缩放时立即重置累积 (`sampleCount = 0`), 避免旧帧数据与新视角不匹配导致的拖影。

3. **RGB 漫反射**: 有色 albedo 乘以 throughput 产生彩色漫反射反弹, 不需要额外的颜色纹理。

4. **萤火虫钳制**: `min(color, 10.0)` 在累积前执行, 防止个别像素的极端高亮污染邻域。

5. **静态 BVH 局限**: BVH 仅在启动时构建一次, 不支持场景中物体移动/旋转/添加/删除。如需动态场景需重新构建 BVH。

## 验证步骤

1. `premake5.exe vs2022` 重新生成解决方案
2. VS2022 打开 `Tsundere.sln`, Debug/Win32 编译
3. 启动 SandBox, 在 State 面板勾选 "Path Trace?"
4. 初始帧较嘈杂, 保持相机静止观察噪声逐渐收敛
5. 移动相机确认累积自动重置 (样本数归零, 噪声恢复)
6. 场景背景应显示天空盒 (光线未命中时)
7. 取消勾选确认回退到正常 Blinn-Phong 光栅化渲染
8. 缩放视口确认累积正确重置

## 后续方向

- 从 Material 组件提取真实材质数据 (diffuse 纹理采样铝色, 金属度/粗糙度)
- Cook-Torrance PBR BRDF (GGX 法线分布 + Smith-G 几何衰减 + Fresnel-Schlick)
- 重要性采样 (灯光采样 + 材质采样, MIS 多重要性采样混合)
- 折射/透射材质 (玻璃, 水)
- 降噪 (SVGF / OIDN)
- 动态 BVH 重构 (物体变换时增量更新)
- 多线程 BVH 构建 (SAH 启发式, CWBVH)
