# OpenGL / Vulkan 双后端 GPU Path Tracing 实施方案

本文是交给执行模型的实施任务。请先检查工程实际代码，再按阶段完成实现；不要仅提供建议或停留在方案讨论。保留现有用户改动，避免无关重构。

## 1. 目标与已确定的技术选择

- OpenGL 和 Vulkan 是两个独立后端，一次运行只使用其中一个，不需要 GL/Vulkan 资源互操作。
- OpenGL：Compute Shader 执行完整 PT，复用已有自定义 BVH，使用 GPU 软件遍历和求交。
- Vulkan：Compute Shader 执行完整 PT，使用 `VK_KHR_ray_query` 查询 Vulkan BLAS / TLAS，利用硬件光追能力加速遍历与三角形求交。
- 本阶段不引入 Ray Tracing Pipeline、raygen/hit/miss shader 或 Shader Binding Table（SBT）。
- 共享路径积分、材质、BSDF、采样和累积逻辑；后端分别实现资源管理、加速结构及求交。
- 逐像素、逐射线、逐反弹、材质读取与 BSDF 分派全部在 GPU 上执行。CPU 不参与每次命中后的处理。

“完整 PT 在 GPU 上执行”不意味着专用光追单元执行整个积分器：通用 shader 执行材质和采样等计算，专用光追能力用于遍历和求交。

沿用工程实际的后端选择方式。README 当前展示的是 Premake `--renderer=vulkan` 构建选项，不应仅为本任务新增运行时热切换机制。

## 2. 开始前的代码检查

阅读仓库约定和相关架构文档，检查并简要汇报以下现状，然后继续实施：

1. RHI、OpenGL/Vulkan 后端，以及真实的后端选择和构建方式。
2. 现有 BVH 的构建位置、节点布局、实例支持、遍历实现，以及是否已有 GPU 版本。
3. mesh、实例、材质、纹理、相机和灯光的数据组织及更新机制。
4. shader 编译、反射、include、宏和后端变体机制。
5. 现有 PT、随机采样、累积、显示和调试工具。
6. RDG 的 compute、buffer、持久化历史资源和同步支持。

项目文档所示入口包括：

- `T_Core/src/Pipeline/`：RDG 和渲染 Pass。
- `T_Core/src/Platform/`：RHI、API 后端和 ShaderCompiler。
- `T_Core/src/Scene/`：场景及 ECS。
- `SandBox/`：应用和编辑器入口。
- `overview/RDG_Architecture_Overview.md`：RDG 架构参考。

这些是检查入口，不代表已审核当前实现。以实际代码为准，优先复用现有能力。合理的实现细节自行决定并记录。

## 3. 架构与职责

```text
CPU：场景管理、上传资源、安排更新、提交命令
                         |
                 共享 GPU PT 主循环
                 /                \
       intersectScene(ray)      occluded(ray)
                 \                /
                  后端求交适配层
                 /                \
       OpenGL 自定义 BVH       Vulkan Ray Query
                 \                /
                 统一的命中信息
                         |
          表面重建 → 材质读取 → BSDF / 采样
                         |
                 下一次反弹 / 累积输出
```

Pass 通过现有 RHI 和 RDG 工作，API 细节留在后端。若抽象缺少必要能力，增加最小且明确的接口，不在通用 Pass 中散布底层 API 调用。

公共 shader 代码尽量通过 include 和薄适配层复用。允许两个后端拥有不同入口及绑定声明，不强迫整个 shader 文件完全相同。特别检查 Vulkan Ray Query 扩展和加速结构描述符不会进入 GL 的 SPIRV-Cross 转译路径。

## 4. 共享射线与命中契约

定义语义一致的数据和接口，具体结构可适配现有工程：

```cpp
// 概念接口，不要求直接使用以下 C++ 布局。
struct Ray {
    vec3 origin;
    vec3 direction;
    float tMin;
    float tMax;
};

struct Hit {
    bool found;
    float t;
    uint instanceID;
    uint geometryID;
    uint primitiveID;
    vec2 barycentrics;
    bool frontFace;
};

Hit intersectScene(Ray ray); // 最近有效命中
bool occluded(Ray ray);     // 指定区间内是否存在有效遮挡
```

必须明确：

- `origin` 和 `direction` 的坐标空间，以及 `t` 在 `origin + t * direction` 中的含义。
- 实例变换下的参数一致性，尤其非均匀缩放；避免局部射线归一化后直接比较不一致的距离。
- 实例、几何、图元 ID 到原始索引和材质数据的映射。
- 重心坐标分量约定、正反面及背面剔除策略。
- `tMin/tMax`、自相交偏移及有限长度阴影射线的处理。

命中后在 GPU 上重建位置、几何法线、着色法线、UV，正确使用法线变换并处理镜像实例。使用实例/几何/图元映射查找材质。

GPU buffer 的宿主与 shader 布局必须显式匹配；上述概念结构中的 bool/vec3 不可未经验证直接作为 CPU/GPU 二进制布局。

## 5. GPU 材质与共享积分器

材质参数存放在 GPU buffer 和纹理中。通过材质类型在 shader 中调用对应 BSDF，无需自建 SBT，也无需 CPU 分派：

```glsl
// 概念伪代码
Material mat = materials[materialID];
switch (mat.type) {
    case DIFFUSE: sampleDiffuse(...); break;
    case METAL:   sampleMetal(...);   break;
    case GLASS:   sampleGlass(...);   break;
}
```

以上类型用于说明分派方式，不要求第一阶段新增工程尚未支持的全部材质。

优先复用已有积分器；不存在完整实现时，先用不透明漫反射和简单光源/环境跑通闭环：

1. 生成带采样扰动的相机射线。
2. 查询最近有效命中。
3. 未命中时累加环境贡献并结束路径。
4. 命中时重建表面并读取材质。
5. 累加自发光和直接光照，必要时发射遮挡射线。
6. 采样 BSDF，按 PDF 和正确权重更新路径吞吐量。
7. 生成下一条射线，执行最大深度和其他终止条件。
8. 写入像素累积与样本计数。

已有的光源采样、MIS、俄罗斯轮盘赌应共享或复用。新增时避免直接光照与命中发光体/环境的贡献重复计数，正确处理 delta BSDF 和零 PDF 等情况。

初始实现使用一个 compute invocation 处理一个像素样本，在 shader 内循环反弹。暂不引入 wavefront 队列。随机序列应结合像素、样本和维度，避免各像素或各帧重复相同样本。

## 6. OpenGL 后端

- 复用现有 BVH 构建和已有 GPU 数据。
- 若遍历只在 CPU 上，补充 GPU 节点/几何布局和 Compute Shader 遍历。
- 正确处理最近命中、任意遮挡、射线区间、实例变换及遍历栈容量，不能静默丢失溢出节点。
- 完成 PT compute dispatch、累积纹理及输出显示。
- 通过现有 RDG/RHI 设置计算写入到后续读取之间所需的屏障。
- 不要求 GL 获得专用光追能力。

## 7. Vulkan 后端

### 7.1 功能检测和 shader 支持

- 检测并启用 Ray Query、Acceleration Structure、Buffer Device Address 所需功能、扩展及版本相关依赖。
- 不将“支持 Vulkan 1.3”等同于“支持 Ray Query”。
- 按实际编译器设置正确的 Vulkan/SPIR-V 目标环境，支持加速结构描述符反射和绑定。
- 能力不足时给出清楚诊断，遵循已有后端选择/降级策略；不要静默显示黑屏，也不要自行新增跨后端切换机制。

### 7.2 加速结构

- 从原始顶点和索引构建 BLAS，从 BLAS 实例和变换构建 TLAS。
- 多个实例尽量复用同一 mesh 的 BLAS。
- Vulkan PT 不需要先构建自定义 BVH。
- 正确处理 buffer usage、device address、scratch 对齐、构建容量、句柄和资源生命周期。
- 静态几何尽量只构建一次；实例变化更新/重建 TLAS；几何变形更新/重建相应 BLAS。
- 仅当原始 build flags 和更新约束允许时使用 update，否则重建。
- BLAS 地址或实例集合变化时正确刷新 TLAS，不提前释放仍被在途帧引用的资源。

### 7.3 求交与同步

- 在 Compute Shader 内用 Ray Query 实现 `intersectScene` 和 `occluded`。
- 最近命中查询使用完成遍历后的有效 committed hit；遮挡查询可以在找到有效遮挡后提前终止。
- 正确处理上传、BLAS 构建、TLAS 构建、compute 读取及输出显示之间的依赖和内存可见性。
- 如现有 RDG 不能表达加速结构依赖，补充最小必要表达或明确封装同步；不能假定普通纹理屏障足够。
- 第一阶段以不透明三角形为范围。场景需要 alpha cutout 时，在候选命中阶段判定透明度，再决定是否确认命中，阴影查询同样处理。

## 8. CPU、累积和显示

CPU 负责场景管理、资源上传、必要的加速结构更新、参数设置和提交命令。正常渲染路径禁止逐射线读回、逐反弹等待 CPU，以及每帧为显示而读回整张图像。

累积要求：

- 使用适当精度的持久化历史资源和样本计数，不能每次 RDG 执行都丢失历史。
- 相机、几何、实例变换、材质、灯光或影响积分结果的参数变化时重置累积。
- 窗口/视口尺寸变化时正确重建相关资源并重置样本计数。
- PT 开关、场景切换和资源重建后的历史状态必须有效。
- 在线性空间累积，在显示阶段处理曝光、色调映射和输出色彩转换，避免重复 gamma 转换。
- GPU 时间统计和必要的调试读回可以异步执行，不放入逐射线主循环。

## 9. 分阶段实施

1. **检查现状**：确认可复用模块、缺失能力和最小修改范围。
2. **共享契约**：整理命中结构、表面重建、材质访问与求交接口。
3. **GL 闭环**：跑通 GPU 软件 BVH PT、累积和显示，作为对照基线。
4. **Vulkan 求交**：完成功能检测、BLAS/TLAS、描述符和 Ray Query。
5. **共享 PT 接入**：让 Vulkan 使用同一积分逻辑，完成 RDG、更新及历史管理。
6. **验证与测量**：修复两后端差异，交付可复现的运行步骤和性能数据。

每阶段保持可编译。已有实现满足某阶段时直接复用，不重复开发。先交付最小可运行闭环，再补齐现有场景必需功能；不把新增高级材质、降噪器或 wavefront 调度作为本阶段完成的前提。

## 10. 验收与性能比较

采用小型、结果可判断的场景，至少验证：

- 最近命中、未命中、有限距离阴影和遮挡提前终止。
- 同一 mesh 多实例、旋转、非均匀缩放及镜像变换。
- 法线/UV 插值、材质映射和多材质几何。
- 环境贡献、直接光照及多次反弹。
- 相机/场景变化、resize、PT 开关后的累积重置。
- 两后端在同一场景、分辨率、样本数、反弹数和材质参数下的结果一致性，允许合理浮点和随机噪声差异。
- Vulkan validation layer 错误及现有 GL 调试错误。

对确定性射线可比较命中有无、距离与重建位置；共享边或等距几何的图元 ID 可以不同，不应将合法的边界差异误判为失败。

性能使用 GPU 时间戳测量，区分：

1. BVH/BLAS/TLAS 初始构建及更新成本（CPU 构建另行统计 CPU 耗时）。
2. PT compute dispatch GPU 耗时。
3. 显示和其他后处理开销。

排除 shader 首次编译和预热影响，使用相同工作量。不预设 Ray Query 的加速倍率，不把本次两后端比较表述为 Ray Query 与 RT Pipeline 的性能比较。

## 11. 最终交付

- 实际完成的代码、shader 和必要配置。
- 修改位置及关键设计说明。
- 两后端准确的构建、启动、PT 开启和参数设置方式。
- 验证场景、执行结果、性能条件和数据。
- 尚未支持的材质/几何/更新方式及已知限制。

缺少 GPU、SDK 或运行环境时，明确区分“完成代码”“通过编译”“实际运行验证”；不能把未执行的检查写成通过。测试应针对正确性和关键边界，不为可逆的文档或薄适配重复编写无价值测试。

## 12. Vulkan 官方参考

- [Ray Tracing 扩展与 Ray Query / RT Pipeline 区别](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html)
- [Ray Traversal 规范](https://docs.vulkan.org/spec/latest/chapters/raytraversal.html)
- [Ray Query Rendering 示例](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Advanced_Topics/Ray_Query_Rendering.html)

执行时依据当前使用的 SDK、头文件和官方规范核实接口细节。
