# Tsundere

基于 C++17 的渲染引擎,四层架构 + 可插拔图形后端(OpenGL 4.5 / Vulkan 1.3):

```
┌─ 应用层          SandBox(编辑器视口、相机交互、RDG 驱动)
├─ 场景与管线层     T_Core:EnTT ECS 场景 + RDG 渲染依赖图
├─ 硬件抽象层       T_Core::Platform::RHI(统一命令缓冲 / 管线 / 纹理 / 描述符)
└─ 图形 API 层      OpenGL 4.5(VAO)/ Vulkan 1.3(dynamic rendering)
```

核心特性:

- **RDG(Render Dependency Graph)**:Pass 声明读写,自动拓扑排序、推导内存屏障、管理瞬态资源生命周期
- **双后端**:同一套 Pass 代码跑 OpenGL 和 Vulkan,`--renderer=vulkan` 一键切换
- **自动着色器管线**:GLSL → shaderc → SPIR-V → SPIRV-Cross 反射(GL 后端还会转回 GLSL 430),运行时编译,改完 `.glsl` 重启即生效
- **ImGui 编辑器**: docking 布局、组件检查器、RenderPass 管理面板(运行时开关 Pass)

参考项目:[The Cherno](https://www.youtube.com/@TheCherno)、[LearnOpenGL](https://learnopengl-cn.github.io/)

## 快速开始

前置要求(详见 [DEPENDENCIES.md](DEPENDENCIES.md)):

| 项目 | 要求 |
| ---- | ---- |
| 系统 | Windows 10/11 x64 |
| IDE | Visual Studio 2022("使用 C++ 的桌面开发"工作负载;其他 VS 版本也可以) |
| Git、curl | curl Win10 自带 |

一条命令准备全部第三方依赖(含自动克隆并编译 assimp、静默安装 Vulkan SDK,后者会弹 UAC):

```bat
git clone https://gitee.com/Waibi_waibi/Tsundere.git
cd Tsundere
setup_deps.bat -BuildAssimp -InstallVulkanSDK

:: 生成 VS 工程(OpenGL 后端,默认)
premake5.exe vs2022
:: 或 Vulkan 后端
premake5.exe vs2022 --renderer=vulkan
```

打开 `Tsundere.sln`,选 `Debug | x64` 编译,运行 `bin/SandBox.exe`。

**不用手动配置任何库路径**:assimp 库名、Vulkan SDK 路径、Windows SDK 版本由 `premake5-defs.lua`
在生成工程时自动探测(探测结果会打印出来),换了 VS 版本或 SDK 版本也不需要改代码。

常用脚本操作:

```bat
setup_deps.bat -CleanCache          :: 清理 %TEMP% 下载/构建缓存(~1 GB),已装依赖不受影响
setup_deps.bat                      :: 补缺检查(已装的自动跳过)
setup_deps.bat -Force -SkipAssimp   :: 强制重新下载除 assimp 外的所有库
```

## 构建

`premake5 vs2022` 生成解决方案,VS2022 中 `Debug | x64` 编译。产物:

```
bin/
├── SandBox.exe          编辑器主程序
├── T_Core.dll           引擎核心
└── *.dll                glew32 / assimp / shaderc / spirv-cross(post-build 自动拷贝)
```

C++ 标准 C++17,运行时 `/MDd`(Debug),仅 x64。

## 代码结构

```
SandBox/          主程序与示例层
T_Core/
  src/Core/       基础设施(Asset、Resource、Serializer)
  src/Scene/      ECS(Entity、Transform、Camera、Light)
  src/Pipeline/   RenderGraph、RenderGraphPass、各渲染 Pass
  src/Platform/   RHI 抽象 + GL / Vulkan 后端 + ShaderCompiler
  src/Panels/     ImGui 面板与组件检查器
T_Tool/           工具库(字符串、格式、内存)
T_UI/             ImGui 布局与面板基础设施
```

### 添加一个渲染 Pass

1. 在 `T_Core/src/Pipeline/Passes/` 新建 `MyPass.h/.cpp`,继承 `RenderGraphPass`
2. `Setup(builder)`:用 `builder.ReadTexture() / WriteTexture()` 声明读写(纯声明,不碰物理资源)
3. `Execute(ctx)`:用 `ctx.GetTexture(handle)` 解码物理资源,经 `ctx.GetCommandBuffer()` 录制 GPU 工作
4. 文件末尾 `REGISTER_RENDER_PASS("MyPass")` 自动注册
5. 在 RenderPassManager 面板运行时开关,无需改代码

Pass 只依赖 `RenderGraphBuilder` + `RHICommandBuffer`,完全不感知具体图形 API——
屏障插入、布局转换、瞬态内存分配全部由 RDG 在编译期推导。

## 依赖

| 库 | 版本 | 用途 |
| ---- | ---- | ---- |
| ImGui | 1.92.3-docking(固定) | 编辑器 UI |
| glm | 1.0.3 | 数学库 |
| spdlog | 1.17.0 | 日志 |
| stb_image | 2c980bb | 图片加载 |
| EnTT | 3.15.0(固定) | ECS |
| GLEW / GLFW | 2.3.1 / 3.5.1 | OpenGL 加载 / 窗口 |
| assimp | 6.0.5 | 模型导入(40+ 格式,含 PMX) |
| Vulkan SDK | 1.4.357.0(推荐,自动探测) | 两个后端都需要(shaderc / SPIRV-Cross 编着色器) |

版本、放置位置、手动构建方法、升级注意事项(ImGui/EnTT 有不兼容版本)全部见
[DEPENDENCIES.md](DEPENDENCIES.md)。

## 常见问题

**启动报 `0xc000007b`**:bin/ 里混进了 x86 的 dll或缺 dll。确认 `glew32.dll`、`assimp-*-mtd.dll`
等是 x64,并重新编译一次(post-build 会重新拷 dll)。更多排查见 [DEPENDENCIES.md](DEPENDENCIES.md) 常见问题表。

**着色器报错**:着色器在启动时编译(GLSL → SPIR-V),看控制台的 shaderc 输出定位 `.glsl` 行号;
没有着色器缓存,每次启动都会重编。

**切换后端后链接失败**:两个后端共用一套依赖,但切换要重新生成工程
(`premake5 vs2022 --renderer=vulkan` 或去掉该参数),然后重新编译。

## 许可

见 [LICENSE](LICENSE)。
