# 外部依赖说明

`T_Core/vender/*` 和 `Dependence/*` 都在 `.gitignore` 里,clone 下来只有空目录。
`setup_deps.bat` 一条命令把所有第三方库下载、编译、安放到位;`premake5-defs.lua`
在生成工程时自动探测本机的 assimp 库名、Vulkan SDK 和 Windows SDK,不需要手动配置任何路径。

本文讲清四件事:**脚本怎么用**、**它替你做了什么**(手动安装参考)、**升级依赖要注意什么**、**出问题怎么排查**。

## 快速开始

```bat
:: 一键准备(在仓库根目录执行)
setup_deps.bat -BuildAssimp -InstallVulkanSDK

:: 生成 VS 工程并编译
premake5.exe vs2022                    :: OpenGL 后端(默认)
premake5.exe vs2022 --renderer=vulkan  :: Vulkan 后端
:: 打开 Tsundere.sln,Debug | x64 编译,产物在 bin/
```

全部开关:

| 开关 | 作用 |
| ---- | ---- |
| `-BuildAssimp` | 克隆 assimp 并用 CMake 编译(首次必须) |
| `-InstallVulkanSDK` | 下载并静默安装 Vulkan SDK(弹 UAC;已装则跳过) |
| `-AssimpSource <目录>` | 复用自己编好的 assimp(要求 `<目录>\build\lib\Debug` 下有产物) |
| `-Force` | 已存在的库也重新下载覆盖 |
| `-SkipAssimp` | 跳过 assimp |
| `-CleanCache` | 清理 `%TEMP%\tsundere_deps` 下载/构建缓存(~1 GB),已装依赖不受影响 |

脚本位置 `scripts/setup_deps.ps1`。缓存目录 `%TEMP%\tsundere_deps` 不会自动清理,
空间紧张时用 `-CleanCache` 删,下次按需重新下载。

## 自动探测(新机器免配置)

`premake5 vs2022` 时,`premake5-defs.lua` 按以下顺序探测,结果打印在控制台:

| 探测项 | 顺序 |
| ---- | ---- |
| assimp 库名 | 扫 `Dependence/lib/assimp/assimp-*-mtd.lib`,取实际文件名 |
| Vulkan SDK | `VULKAN_SDK` 环境变量 → `C:\VulkanSDK\` 下最新版本目录 |
| Windows SDK | `WindowsSDKVersion`/`WindowsSdkDir` 环境变量 → `Windows Kits\10\Include` 下最新版本 |

```
premake: assimp library   = assimp-vc143-mtd
premake: Vulkan SDK root  = C:\VulkanSDK\1.4.357.0
premake: Windows SDK ver  = 10.0.22621.0
```

因此:**换 VS 版本**(assimp 库名会变成 `vc145` 等)、**换 SDK 版本**,都不需要改 premake 文件。
探测失败时 premake 打 WARNING 并退回默认值,直到链接才报错——看到 WARNING 先跑 `setup_deps.bat`。
`setup_deps.ps1` 的 Verify 段用同样的探测逻辑做最后检查,缺什么标 `[MISS]` 并返回非 0。

## 环境要求

| 项目 | 要求 |
| ---- | ---- |
| 系统 | Windows 10/11 x64(只支持 x64,所有库必须 x64) |
| IDE | Visual Studio 2022 + "使用 C++ 的桌面开发"(其他 VS 版本也可以,assimp 库名自动跟随工具集) |
| 工具 | git、curl(Win10 自带);CMake 用 VS 自带的即可,只有编 assimp 时用 |
| Vulkan SDK | 两个后端都需要(着色器编译),见下文 |

## 依赖一览

| 库 | 版本 | 形式 | 放置位置 | 来源 |
| ---- | ---- | ---- | ---- | ---- |
| ImGui | **1.92.3-docking**(固定) | 源码 | `T_Core/vender/imgui/` | [ocornut/imgui](https://github.com/ocornut/imgui/releases/tag/v1.92.3-docking) |
| glm | 1.0.3 | 纯头文件 | `T_Core/vender/glm/` | [g-truc/glm](https://github.com/g-truc/glm/releases/tag/1.0.3) |
| spdlog | 1.17.0 | 纯头文件 | `T_Core/vender/spdlog/` | [gabime/spdlog](https://github.com/gabime/spdlog/releases/tag/v1.17.0) |
| stb_image | master @ `2c980bb` | 单头文件 + 1 个 cpp | `T_Core/vender/stb_image/` | [nothings/stb](https://github.com/nothings/stb) |
| EnTT | **3.15.0**(固定) | 单头文件 | `T_Core/vender/entt/` | [skypjack/entt](https://github.com/skypjack/entt/releases/tag/v3.15.0) |
| GLEW | 2.3.1 | 预编译 x64 | `Dependence/include/GL`、`lib/GLEW` | [nigels-com/glew](https://github.com/nigels-com/glew/releases/tag/glew-2.3.1) |
| GLFW | 3.5.1 | 预编译 x64(静态链接) | `Dependence/include/GLFW`、`lib/GLFW` | [glfw.org](https://www.glfw.org/download.html) |
| assimp | 6.0.5 | 源码,CMake 编译 | `Dependence/include/assimp`、`lib/assimp` | [assimp/assimp](https://github.com/assimp/assimp/releases/tag/v6.0.5) |
| Vulkan SDK | 1.4.357.0(推荐,版本可不同) | 安装包 | `C:\VulkanSDK\<版本>` | [LunarG](https://vulkan.lunarg.com/sdk/home) |

标"固定"的两个库(ImGui、EnTT)不能随便升级,原因见[升级注意事项](#升级注意事项)。

代码里 include 的写法是 `"imgui/imgui.h"`、`<glm/glm.hpp>`、`"spdlog/spdlog.h"`、
`"stb_image/stb_image.h"`、`<entt/entt.hpp>`,include 根目录是 `T_Core/vender/` 和
`Dependence/include/`,每个库平铺在自己的目录下。

## 脚本做了什么(手动安装参考)

正常情况下不需要读这一节——`setup_deps.bat` 自动完成以下全部操作。仅当要在无脚本的
环境里手工搭依赖、或想改脚本行为时作参考。

- **ImGui**:必须 docking 分支;拷根目录 `*.h`/`*.cpp`,再从 `backends/` 拷
  `imgui_impl_glfw.*`、`imgui_impl_opengl3.*`(含 `imgui_impl_opengl3_loader.h`)、
  `imgui_impl_vulkan.*`,与核心文件**平铺**。其他 backend(dx/sdl/win32/…)不要拷,
  多拷会被 premake 编进工程然后缺头文件报错。
- **glm**:拷仓库内层 `glm/` 内容;**删掉 `glm.cppm`**(C++20 模块文件,C++17 下编译失败;
  premake 的 `removefiles " *.cppm"` 只匹配项目根,排除不到它)。
- **spdlog**:只拷 `include/spdlog/` 内容,header-only,不拷 `src/`。
- **stb_image**:拷 `stb_image.h`,并新建 `stb_image.cpp`:
  `#define STB_IMAGE_IMPLEMENTATION` + `#include "stb_image.h"`(工程里没有别处定义实现)。
- **EnTT**:拷 `single_include/entt/entt.hpp`。不要 master(4.x 要求 C++20)。
- **GLEW**:`glew-2.3.1-win32.zip`(包名 win32,内有 x64),取 `include/GL/*` 和
  `lib/Release/x64/*` + `bin/Release/x64/glew32.dll`。**拿错成 Win32 运行时报 `0xc000007b`**。
- **GLFW**:`glfw-3.5.1.bin.WIN64.zip`,取 `include/GLFW/*` 和 `lib-vc2022/*`。
  链接静态 `glfw3.lib`,不需要 dll。
- **assimp**:克隆 v6.0.5,CMake 配置(x64 / `BUILD_SHARED_LIBS=ON` / `USE_STATIC_CRT=OFF` /
  `ASSIMP_BUILD_ZLIB=ON` / 关闭 tests、tools、samples、warnings-as-errors),编 Debug。
  安装 = 源码树 `include/assimp/*`(不含 `*.in`)+ build 树生成的 `config.h`、`revision.h`
  进 `Dependence/include/assimp/`;build 树 `lib\Debug` / `bin\Debug` 下的
  `assimp-*-mtd.lib/.dll/.pdb` 进 `Dependence/lib/assimp/`。
  公开头文件在**源码目录**,build 目录只有两个生成头文件,只看 build 会以为头文件很少。
- **Vulkan SDK**:静默安装
  `vulkansdk-windows-X64-<版本>.exe --root C:\VulkanSDK\<版本> --accept-licenses --default-answer --confirm-command install`。

### Vulkan SDK 为什么两个后端都要

- 两个后端的着色器都由 `ShaderCompiler.cpp` 编译:shaderc(GLSL→SPIR-V)+ SPIRV-Cross C API
  (反射;GL 后端还把 SPIR-V 转回 GLSL 430),两者随 SDK 安装。
- premake 在两个后端下都链接 `vulkan-1.lib`、`shaderc_shared.lib`、`spirv-cross-c-shared.lib`,
  且 `imgui_impl_vulkan.cpp` 总会被编进 `T_Core.dll`。
- **必须用 C API 动态库** `spirv-cross-c-shared`:SDK 里的 SPIRV-Cross 静态库是 Release `/MD`
  编的,与项目的 `/MDd` 冲突(`LNK2038`)。
- 运行时:`vulkan-1.dll` 由显卡驱动提供;`shaderc_shared.dll`、`spirv-cross-c-shared.dll`
  由 T_Core 的 post-build 从 SDK `Bin` 拷到 `bin/`。

## 升级注意事项

### ImGui ≥ 1.92.4(当前固定 1.92.3)

`ImGui_ImplVulkan_InitInfo` 的 `RenderPass`、`Subpass`、`MSAASamples` 被挪进 `PipelineInfoMain`
(≥ 1.92.4),要改 `T_Core/src/Panels/Platform/Vulkan/ImGUIRenderVulkan.cpp`:

```cpp
// 1.92.3 及以前                              // 1.92.4 及以后
initInfo.RenderPass  = context->GetImGuiRenderPass();      initInfo.PipelineInfoMain.RenderPass  = ...;
initInfo.Subpass     = 0;                                   initInfo.PipelineInfoMain.Subpass     = 0;
initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;              initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
```

代码还用了 `ImTextureID_Invalid` 和 `initInfo.ApiVersion`,所以也不能低于 1.92.0。

### EnTT ≥ 3.16(当前固定 3.15.0)

`entt::meta<T>()` 被删,要改 `T_Core/src/Panels/ComponentInspectors.h`:

```cpp
// 3.15 及以前                              // 3.16 及以后
entt::meta<T>().type(entt::hashed_string{ name });    entt::meta_factory<T>{}.type(entt::hashed_string{ name });
```

EnTT 4.x(master)要求 C++20,premake 的 `cppdialect` 也要改,改动面更大。

### assimp / Vulkan SDK / Windows SDK 换版本

- assimp:核心 API(`Assimp::Importer`、`aiScene/aiMesh` 等)5.x→6.x 未变,换 tag 重跑
  `-BuildAssimp` 即可;库名跟工具集自动匹配。
- Vulkan SDK:直接装新版,premake 自动探测;`-InstallVulkanSDK` 下载的版本由
  `setup_deps.ps1` 顶部 `$V.VulkanSDK` 决定,想换安装版本改它。
- Windows SDK:随 VS 安装,无需任何操作。

### ImGui 的 DLL 导出(已配置,勿动)

ImGui 编进 `T_Core.dll`:T_Core 侧 `IMGUI_API=__declspec(dllexport)`,SandBox 侧 dllimport,
都已在 premake 定义。`imconfig.h` 保持官方原样,**不要**再在里面定义 `IMGUI_API`,否则重定义,
SandBox 链接报几十个 `LNK2019 ImGui::...`。

## 其他说明

- **运行库**:项目 `/MDd`。官方 `glfw3.lib` 是 `/MD` 编的,可能有 `LNK4098` 警告,不影响;
  assimp 按脚本参数编出来就是 `/MDd`,匹配。
- **Release 配置**链接的也是 assimp 的 Debug 库,只编 Debug 就够。
- **DLL 拷贝**:T_Core post-build 把 `glew32.dll`、`assimp-*-mtd.dll`(按探测名)、
  `shaderc_shared.dll`、`spirv-cross-c-shared.dll` 拷到 `bin/`。GLFW 静态链接无 dll。
- **不要**把第三方库提交进 git:`.gitignore` 已忽略 `T_Core/vender/*`、`Dependence/*`。
- **卸载**:删仓库内 `vender/` + `Dependence/` 文件夹即可重装;缓存用 `-CleanCache`;
  Vulkan SDK 从 Windows 设置 > 应用卸载。

## 常见问题

| 现象 | 原因 / 解决 |
| ---- | ---- |
| 启动报 `0xc000007b` | 混进 x86(Win32)的 dll,或 `bin/` 缺 dll。检查 `glew32.dll`、assimp dll 是 x64,重新编译让 post-build 重拷 |
| `entt.hpp` 报 `bit_ceil`、`same_as` 不是 `std` 成员 | EnTT 拿成了 4.x(C++20),换 3.15.0 |
| `initInfo.RenderPass` 不是成员 | ImGui ≥ 1.92.4,见升级注意事项 |
| `LNK2019 ImGui::Begin ...` | `IMGUI_API` 未导出;重新跑 `premake5 vs2022` 重新生成 |
| `LNK2019 stbi_load` | 缺 `stb_image.cpp`(脚本会自动生成) |
| glm 报 C++17 语法错误 | `vender/glm/glm.cppm` 没删(脚本自动删,手动安装时容易漏) |
| 找不到 `vulkan/vulkan.h` 或 `vulkan-1.lib` | Vulkan SDK 没装;跑 `premake5 vs2022` 看 "no Vulkan SDK found" WARNING,装好后**重新生成工程** |
| 找不到 `spirv_cross/spirv_cross_c.h` | SDK 安装时勾掉了 SPIRV-Cross 组件,重装 SDK 默认就有 |
| 启动缺 `shaderc_shared.dll` / `spirv-cross-c-shared.dll` | post-build 没跑成功(看 SDK 探测路径),或手动从 SDK `Bin` 拷到 `bin/` |
| `LNK2038 RuntimeLibrary` 不匹配(spirv-cross) | 链了 SDK 的 SPIRV-Cross **静态库**,改用 `spirv-cross-c-shared.lib` |
| assimp 编译报 zlib 错误 | `ASSIMP_BUILD_ZLIB=ON`,用 assimp 自带 zlib(否则可能链到 Anaconda 里架构不对的 `z.lib`) |
| 找不到 `assimp/config.h` | 只拷了源码头文件,漏了 build 目录生成的 `config.h`、`revision.h`(脚本自动处理) |
| premake WARNING "no assimp import library" | `Dependence/lib/assimp/` 为空,跑 `setup_deps.bat -BuildAssimp` |
