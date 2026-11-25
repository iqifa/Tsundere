# GPU Path Tracing（OpenGL / Vulkan 双后端）

对应实施方案 `GPU_PathTracing_Implementation_Plan.md`。本文说明现状检查结论、架构、射线/命中契约、两后端实现、使用方式、验证状态和已知限制。

> **状态说明（务必先读）**
> - **完成代码**：下文列出的全部 C++ / GLSL。
> - **通过编译**：只有与 API 无关的部分在 Linux g++/clang 下编译通过：`Scene/BVH.cpp`、`Scene/BVHBuilder.cpp`、`Pipeline/PathTracing/PathTraceScene.cpp`（后两者用桩头文件 + 真实 EnTT/glm 做语法检查），`tests/BVHTests.cpp` 运行通过（含 ASan/UBSan）。
> - **未验证**：MSVC 编译、两个后端的实际运行、GLSL 经 shaderc 的编译、Vulkan validation layer、性能数据。开发环境没有 Windows 工具链、Vulkan SDK 头文件和 GPU。第一次在本机构建时请按“验证步骤”逐项确认。

## 1. 现状检查结论

| 项目 | 检查结果 | 处理 |
|---|---|---|
| 后端选择 | 编译期：`premake5 vs2022 [--renderer=vulkan]` → `OpenGL_For_Render` / `Vulkan_For_Render`；无运行时切换 | 沿用，不新增切换机制 |
| BVH | `Scene/BVH.{h,cpp}` 两级 SAH BVH（上一阶段完成），有 GPU 布局和 CPU 参考遍历，无 GPU 遍历 | 新增 GLSL 软件遍历；新增“平铺几何”模式给 Vulkan BLAS 用 |
| 旧 PT | `PathTracePass.h` / `PathTrace.shader`：GL bindless、单层 BVH，已被 premake 排除，不可用 | 不复用，原样保留；新 Pass 另起文件 |
| 场景数据 | EnTT：`Transform`（本地变换）+ `MeshRender`（模型路径 + 每子网格材质）+ `Light`；网格 CPU 数据常驻 `Mesh::vertices/indices`；`Scene::Revision` 只在加载场景时递增 | PT 侧每帧哈希检测几何/变换/材质/灯光变化 |
| 材质 | `Engine::Material` 参数表（`u_BaseColor`、`u_EmissiveColor`、`u_EmissiveStrength`、`u_Roughness`、`u_Metallic`）+ 贴图 | 常量参数与底色贴图；底色读取契约见下文 |
| Shader | `.shader` 分段 + `#include`；shaderc → SPIR-V；GL 再经 SPIRV-Cross 转 GLSL 430；预定义 `TS_OPENGL` / `TS_VULKAN`；Vulkan 目标 env = Vulkan 1.2 | Ray Query 代码只放在 Vulkan 入口文件，GL 永不编译到 |
| GL sampler/image binding | `FixupForOpenGL` 只保留入口文件里写明的 binding，include 里的会被重新分配 | PT 的 `image2D` / `samplerCube` 在入口 `.shader` 声明 |
| RDG compute | 无附件的 pass 直接 `Execute`；GL 有 `glMemoryBarrier`；Vulkan 的 `ResourceBarrier` 是空实现、无 compute pipeline、push constant 只给 vertex stage、descriptor 绑定点写死 graphics | 补齐（见 §4.3） |
| 持久化历史 | TAA 的做法：Pass 持有 `RHITexture2D`，每次 Setup 导入 RDG | PT 同样做法 |
| 计时 | 无 GPU 计时设施 | 新增 `RHIGpuTimer` |

## 2. 架构

```text
CPU  PathTraceScene::Update（每帧一次，主线程）
     ├─ 几何/实例/材质分配变化 → BVHBuilder 采集 → 树（GL）或平铺（VK）→ 上传
     │                                         └→ RHIRayTracingScene::Build（VK BLAS/TLAS）
     ├─ 材质参数变化 → 只传材质 buffer
     └─ 灯光变化     → 只传灯光 buffer
GPU  一次 compute dispatch（8×8，一 invocation 一像素）
     PathTraceIntegrator.glsl  相机射线 → 求交 → 表面重建 → 材质 → NEE → BSDF 采样 → 轮盘赌 → 累积
          │ PT_IntersectScene / PT_Occluded
          ├─ PathTraceTraceBVH.glsl       软件两级 BVH（GL；VK 无 ray query 时回退）
          └─ PathTraceTraceRayQuery.glsl  VK_KHR_ray_query（仅 Vulkan）
```

逐像素、逐射线、逐反弹、材质读取和 BSDF 分派都在 GPU 上；CPU 不读回任何东西。

### 文件

| 文件 | 内容 |
|---|---|
| `T_Core/src/Pipeline/Passes/PathTracingPass.h` | `GPUPathTracePass`（RDG 名 `PathTrace`）：变体选择、累积纹理、UBO、描述符、派发、计时、设置/统计 |
| `T_Core/src/Pipeline/PathTracing/PathTraceScene.{h,cpp}` | CPU 场景表、变化检测、材质/灯光打包、GPU 结构体（`static_assert` 校验布局） |
| `T_Core/src/Platform/RHI/RHIRayTracing.h` | 最小加速结构抽象 `RHIRayTracingScene`（Build / IsSupported / 原因） |
| `T_Core/src/Platform/RHI/RHIGpuTimer.h` | 异步 GPU 时间戳 |
| `T_Core/src/Platform/Vulkan/VulkanRayTracing.{h,cpp}` | BLAS/TLAS 构建、device address buffer、函数指针加载 |
| `T_Core/src/Platform/Vulkan/VulkanGpuTimer.cpp`、`GL/GLGpuTimer.cpp`、`GL/GLRayTracing.cpp` | 计时器、GL 的“不支持”实现 |
| `res/shaders/PathTrace{BVH,BVHSky,RayQuery,RayQuerySky}.shader` | 4 个薄入口（求交方式 × 有无天空盒） |
| `res/shaders/include/PathTrace{Common,Integrator,TraceBVH,TraceRayQuery}.glsl` | 共享数据/积分器与两个求交适配层 |

## 3. 射线 / 命中契约

- `PTRay { origin, tMin, direction, tMax }`：世界空间；`direction` 由调用方归一化，所以 `t` 就是世界距离，`P = origin + t·direction`；有效区间是开区间 `(tMin, tMax)`。
- **实例变换**：物体空间射线 = `worldToObject × 世界射线`，方向**不**归一化，`t` 在两个空间相同；非均匀缩放下距离可以直接比较（GL 软件遍历与 Vulkan 硬件遍历都满足，后者由规范保证）。
- **ID 映射**：`instance` = `u_BVHInstances` 下标（GL：TLAS 叶子顺序；VK：TLAS `instanceIndex`，平铺几何按输入顺序排列），`primitive` = 网格内三角形下标，全局三角形 = `instance.triangleOffset + primitive`；材质 = `instance.materialIndex`。`geometry` 恒为 0（每个 BLAS 一个几何）。
- **重心**：`(u, v)`，`P = (1-u-v)·v0 + u·v1 + v·v2`，与 Vulkan 规范一致。
- **正反面 / 剔除**：不剔除（VK 实例标 `TRIANGLE_FACING_CULL_DISABLE`）。表面重建用 `M^-T · cross(e1, e2)` 求世界几何法线，`dot(Ng, dir) < 0` 为正面；镜像实例（det < 0）绕序翻转不影响结果。法线翻到入射一侧，插值法线若越过几何面则退回几何法线。
- **自相交 / 阴影**：起点沿出射侧几何法线偏移 `1e-4 × max(1, |P|∞)`，`tMin = 0`；阴影射线 `tMax = 距离 × (1 - 1e-3)`，方向光为无穷远。
- **栈溢出**：软件遍历栈 TLAS 24 / BLAS 32 项，构建器把树深钳在这两个值内（“近孩子先走、远孩子入栈”每层至多入栈 1 个）；万一溢出置标志、像素输出洋红色，不静默丢节点。
- **宿主/着色器布局**：`GPUBVHNode/Instance/Triangle/TriangleAttrib`、`GPUMaterial`、`GPULight`、`FrameUBO` 都有 `static_assert`；Pass 创建时再用 SPIR-V 反射校验 UBO 大小和每个 SSBO 的数组 stride，不一致直接拒绝运行。

## 4. 实现要点

### 4.1 积分器与材质（共享）

- 底色：MaterialTest 前向、通用 GBuffer、PT 共用 sRGB 取色约定。UNORM 底色贴图显式解码，硬件 SRGB 纹理不重复解码；浮点纹理与 `u_BaseColor` 乘色为线性值，alpha 不做 gamma。统一使用模型 UV，先应用 `u_Tiling` / `u_Offset`，不再由 MaterialTest 单独翻转 Y。过滤在线性空间进行。
- PT 的 `PathTraceBaseColor.shader` 在贴图集合变化时，将已有 GPU 纹理的 mip 0 转入线性 RGBA16F 缓存（SSBO binding 5，每 texel 8 字节，带一圈边界），共享贴图只缓存一次；不从磁盘重载、不读回、不要求 bindless。命中后按 UV 采样缓存；替换贴图也会重置累积。当前底色采样统一为 mip 0，保留 nearest/linear 和 repeat/clamp/border；尚未加入 ray footprint / mip 选择。缓存额外占用显存，受后端 storage-buffer 大小限制。

- 直接光照：MaterialTest、ModelPreview、DeferredLight 和 PT 共用 `include/PBR.glsl`：GGX、Smith、Schlick Fresnel，`F0=mix(0.04,baseColor,metallic)`，金属度连续混合，粗糙度统一为 `alpha=max(roughness²,0.0025)`。PT 不再在金属度 0.5 处切换模型，也不再单独走 delta 镜面。PT 的下一跳用漫反射/GGX 混合采样，权重为完整 `f*cos / mixturePdf`。
- 灯光：三条路径共用 `GatherLocalLights(scene,true)` 收集全部有效灯光（包含多个方向光）；方向/点/聚光共用采样函数，点/聚光统一 `pow(DistanceAttenuation(distance,radius),max(falloff,0.1))`，聚光等内外角是硬边。颜色乘强度一次，漫反射含 `1/PI`，光照在线性 HDR 中累加；前向 SceneColor 改为 RGBA16F，避免亮度在进入后处理前被截到 1。
- 面光：矩形和圆盘统一为单面辐射源。直接光照采用面积均匀采样与 GGX 方向采样的 balance MIS，光栅每灯固定 64 对 Hammersley 样本，PT 每灯每顶点随机一对样本。二者积分对象相同，但固定积分与随机积分有数值/收敛误差；极近、大尺寸或掠射面光仍需单独检验。光栅面光比原代表点近似开销更高。
- 光照分解：场景灯光只走 NEE；自发光几何只在 BSDF 命中时累加；环境只在未命中时累加，三者不重复计数。面光 NEE 内部的两种采样使用 MIS。PT 保留阴影射线与间接光；光栅的 Ambient 仍是独立近似，不纳入直接光照一致性比较。
- 对照方式：PT Debug view 选 **Direct lighting (unoccluded)**，光栅关闭 Ambient/自发光，使用相同相机、材质、灯光、曝光和色调映射，比较相同正面表面。**Direct lighting (shadowed)** 则保留 PT 阴影。当前统一覆盖内置标准材质，不会重写任意自定义材质 shader；法线贴图、透明度与可见性差异仍属于各路径的支持范围。
- 俄罗斯轮盘：从第 3 次反弹起（可调），存活概率 = 吞吐量最大分量（钳到 [0.05, 0.95]）。
- 随机数：PCG hash，种子 = 像素 × 样本号 × 帧种子，路径内按维度依次推进。
- 累积：RGBA32F 线性 HDR，存当前平均值；NaN/Inf 样本丢弃；可选亮度上限（默认关，保持无偏）。显示阶段由后处理做曝光、色调映射和 gamma。

### 4.2 OpenGL

复用 `BVHBuilder`（树模式，5 个 SSBO：binding 8–12）。累积纹理以 image unit 1 读写，天空盒在 texture unit 4。计算写 → 后续采样的 `glMemoryBarrier` 由 RDG（`WriteUAV` → `ReadSRV`）自动插入，跨帧的 image 读写在派发前显式加屏障。

### 4.3 Vulkan

- **能力检测**（`VulkanContext::createLogicalDevice`）：逐项检查 `VK_KHR_acceleration_structure`、`VK_KHR_ray_query`、`VK_KHR_deferred_host_operations`、设备 API ≥ 1.2、`bufferDeviceAddress`、`accelerationStructure`、`rayQuery` feature；全部满足才启用，否则只关闭硬件光追，日志和 Path Tracing 面板写明缺哪一项，PT 回退软件 BVH（不黑屏、不跨后端切换）。描述符池在启用时追加 `ACCELERATION_STRUCTURE_KHR`。
- **Shader**：Ray Query 只在 `PathTraceRayQuery*.shader`（`#version 460` + `GL_EXT_ray_query`），include 里有 `#ifndef TS_VULKAN #error`；目标 env 沿用 Vulkan 1.2（SPIR-V 1.5）。反射新增 `ShaderReflection::AccelerationStructures`。
- **加速结构**（`VulkanRayTracingScene`）：BVHBuilder 平铺模式给出按原始顺序排列的三角形（48 字节/三角形，vec3+pad）。每个非空网格一个 BLAS，直接以该数组为顶点输入（stride 16，无索引），所以 `primitiveIndex` 就是网格内下标；多实例复用同一 BLAS；TLAS 实例按输入顺序，`FORCE_OPAQUE | TRIANGLE_FACING_CULL_DISABLE`。所有 BLAS 放在一个 buffer（256 对齐偏移），scratch 按 `minAccelerationStructureScratchOffsetAlignment` 对齐，TLAS 复用 scratch。带 `DEVICE_ADDRESS` 的内存单独分配（`VulkanBuffer` 不支持）。
- **同步**：上传 → BLAS 构建 → `AS_WRITE→AS_READ|WRITE` 屏障 → TLAS 构建 → `AS_WRITE→COMPUTE 读` 屏障，在一次性命令缓冲里录制并 `vkQueueWaitIdle`（与 `VulkanBuffer::Upload` 同一模式；静态几何只在场景变化时构建）。旧 AS / buffer 走 `VulkanDeletionQueue`（新增通用回调），在途帧结束后才释放。不做 update / compaction：变化一律重建。
- **最近命中**用 `rayQueryProceedEXT` 走完后的 committed hit；**遮挡**加 `gl_RayFlagsTerminateOnFirstHitEXT`。全部不透明，所以没有需要确认的候选命中（alpha cutout 的接入点已在注释里标出）。
- **RHI/RDG 补齐**：Vulkan compute pipeline（`PipelineDesc::isCompute`）、命令缓冲按 pipeline 的绑定点绑定描述符、push constant stage 随 pipeline、`ResourceBarrier` 改为真正的全局内存屏障、`PrepareTextureForStorage`（转 `GENERAL`，RDG 对 `WriteUAV` 纹理自动调用）、描述符集支持存储纹理和加速结构；`VulkanPipeline` 析构改为延迟销毁（UI 关掉 Pass 时帧命令缓冲可能还引用它）。

### 4.4 累积重置

相机 view/proj 变化、几何 / 实例变换 / 材质分配 / 材质参数 / 灯光变化、PT 参数变化、视口尺寸变化（纹理重建）、shader 变体变化（天空盒有无、后端切换）、天空盒内容变化、Pass 关闭再打开（实例重建）、手动 Reset。`maxSamples` 达到后停止派发，保留结果。

## 5. 使用

1. 生成并编译：
   ```bat
   premake5.exe vs2022                    :: OpenGL
   premake5.exe vs2022 --renderer=vulkan  :: Vulkan
   ```
   新文件都在 premake 的 `src/**` 通配里；当前提交的 `T_Core.vcxproj` 是 Vulkan 版，已手工加入新文件，切换后端时请重新生成。
2. 运行 `bin/SandBox.exe`，在 **Render Pass Manager → RayTracing** 勾选 **PathTrace**。出现 **Path Tracing** 窗口：后端、场景统计、参数（反弹数、每帧样本、目标样本、轮盘起点、亮度上限、环境色/强度、调试视图、暂停、重置、Vulkan 上可强制软件 BVH）和计时。
3. 输出是线性 HDR：在 **Window → Post Processing** 打开 **Tone Mapping**（曝光 / gamma）后再看。视口预览 “Scene color (before post)” 显示 PT 原始累积。

## 6. 验证

### 已执行

| 检查 | 结果 |
|---|---|
| `tests/BVHTests.cpp`（g++ -O2、ASan+UBSan、clang） | 通过：空场景、单三角形 t/重心/属性、非法输入过滤、退化分布、深度上限、24 实例（旋转/非均匀缩放/镜像/共享网格）3000 条光线与暴力求交逐条一致、只重建 TLAS、**平铺几何**下标约定与树模式 500 条光线结果一致、26 万三角形构建 |
| 变异测试（上一阶段） | 关掉深度保护 / 颠倒遍历顺序 / 写错逆矩阵，测试都会失败 |
| `PathTraceScene.cpp`、`BVHBuilder.cpp` 语法检查（桩头 + 真实 EnTT/glm） | 无错误无警告 |

### 需要在本机完成（未执行，不要当作已通过）

1. 两个后端分别 MSVC 编译。
2. 两后端启动后日志无 shader 编译错误（`[PathTrace]` / `Shader compile error`）；Vulkan Debug 构建无 validation 错误。
3. 小场景确定性检查：Debug view 切到 **Hit distance / Shading normal / Facing / UV / Instance ID**，两个后端画面应一致（共享边上的 instance/primitive 允许不同）。
4. 多实例 / 旋转 / 非均匀缩放 / 镜像：同一模型放几个实体改 Transform，Facing 视图镜像体仍为绿色（正面）。
5. 有限阴影：点光放在物体后方的墙前，墙后不应被遮挡。
6. 移动相机、改 Transform、改材质参数、改灯光、拖动视口大小、开关 Pass，Samples 计数应归零重新累积。
7. 同场景、同分辨率、同样本数 / 反弹数下两后端结果的噪声级一致性。
8. 性能（见下）。

### 性能测量方式（数据待本机采集）

Path Tracing 面板显示：
- **PT dispatch GPU**：PT compute 的 GPU 时间戳（异步，几帧后可读，从不阻塞）。
- **Whole render graph GPU**：整个 RDG 的 GPU 时间，减去上一项即显示 + 后处理 + 仍在运行的光栅 pass 的开销。
- **Scene rebuild CPU**：采集 + BVH CPU 构建 + 上传；Vulkan 另有 **BLAS / TLAS GPU** 构建时间（时间戳）和包含等待的 CPU 时间。

比较时固定分辨率、样本数/帧、反弹数和场景，等首次 shader 编译和场景构建完成、累积稳定若干帧后再读数；本次对比是“软件 BVH vs Ray Query”，不是 Ray Query 与 RT Pipeline 的对比，也不预设加速倍率。

## 7. 已知限制

- **贴图支持范围**：底色已接入（`u_BaseColorMap` / `texture_diffuse1` / legacy 底色语义）；自发光、粗糙度仍只用材质常量。PT 不执行任意材质 shader，也不支持材质的程序化 checker 模式。
- 材质使用连续 metallic/roughness GGX + 漫反射；无透射 / 玻璃、无法线贴图、无 alpha cutout（接入点已注释）。
- 只用实体的本地 `Transform`（与现有光栅 pass 一致，不处理父子层级）。
- 加速结构 / BVH 变化一律整体重建，无 refit / update / compaction；Vulkan 构建是阻塞的一次性提交。
- 灯光逐个做 NEE（点状灯最多一条、面光最多两条阴影射线），灯多时开销线性增长；环境无重要性采样。
- 光栅 pass 在 PT 开启时仍然运行（便于预览 GBuffer 等），会计入 “Whole render graph GPU”。
- 天空盒按 sRGB 解码成线性；无天空盒时环境为常量色。
