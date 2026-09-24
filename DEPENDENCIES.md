# 外部依赖说明

`T_Core/vender/*` 和 `Dependence/*` 都在 `.gitignore` 里，clone 下来只有空目录和 `.gitkeep`。
所有第三方库需要按本文放好之后才能编译。

## 快速开始

```bat
:: 一键准备（在仓库根目录执行）
setup_deps.bat -BuildAssimp -InstallVulkanSDK

:: 已经自己用 CMake 编好了 assimp，就直接指定源码目录（默认读取 <目录>\build）
setup_deps.bat -AssimpSource F:\assimp

:: 已有的库会跳过，-Force 强制重新下载覆盖
setup_deps.bat -Force -SkipAssimp
```

脚本在 `scripts/setup_deps.ps1`，下载缓存放在 `%TEMP%\tsundere_deps`。最后会逐项检查，缺什么会标 `[MISS]` 并返回非 0。

依赖齐了之后：

```bat
premake5.exe vs2022                    :: OpenGL 后端（默认）
premake5.exe vs2022 --renderer=vulkan  :: Vulkan 后端
```

打开 `Tsundere.sln`，选 `Debug | x64` 编译，产物（`SandBox.exe`、`T_Core.dll` 以及拷过去的 dll）都在 `bin/`。

## 环境要求


| 项目          | 要求                                          | 说明                           |
| ----------- | ------------------------------------------- | ---------------------------- |
| 系统          | Windows 10/11 x64                           | 只支持 x64，所有库都必须是 x64          |
| IDE         | Visual Studio 2022（v143 工具集，"使用 C++ 的桌面开发"） | assimp 的库名 `vc143` 取决于它      |
| Windows SDK | 10.0.22621.0                                | premake 里写死的 `systemversion` |
| Vulkan SDK  | 1.4.357.0，装在 `C:\VulkanSDK\1.4.357.0`       | **两个后端都需要**，见下文              |
| 工具          | git、curl（Win10 自带）、CMake（用 VS 自带的就行）        | CMake 只有编 assimp 时用          |




## 依赖一览


| 库          | 版本                     | 形式                 | 放置位置                                                | 来源                                                                             |
| ---------- | ---------------------- | ------------------ | --------------------------------------------------- | ------------------------------------------------------------------------------ |
| ImGui      | **1.92.3-docking**（固定） | 源码                 | `T_Core/vender/imgui/`                              | [ocornut/imgui](https://github.com/ocornut/imgui/releases/tag/v1.92.3-docking) |
| glm        | 1.0.3                  | 纯头文件（带 1 个 cpp）    | `T_Core/vender/glm/`                                | [g-truc/glm](https://github.com/g-truc/glm/releases/tag/1.0.3)                 |
| spdlog     | 1.17.0                 | 纯头文件               | `T_Core/vender/spdlog/`                             | [gabime/spdlog](https://github.com/gabime/spdlog/releases/tag/v1.17.0)         |
| stb_image  | master @ `2c980bb`     | 单头文件               | `T_Core/vender/stb_image/`                          | [nothings/stb](https://github.com/nothings/stb)                                |
| EnTT       | **3.15.0**（固定）         | 单头文件               | `T_Core/vender/entt/`                               | [skypjack/entt](https://github.com/skypjack/entt/releases/tag/v3.15.0)         |
| GLEW       | 2.3.1                  | 预编译 x64            | `Dependence/include/GL`、`Dependence/lib/GLEW`       | [nigels-com/glew](https://github.com/nigels-com/glew/releases/tag/glew-2.3.1)  |
| GLFW       | 3.5.1                  | 预编译 x64（vc2022）    | `Dependence/include/GLFW`、`Dependence/lib/GLFW`     | [glfw.org](https://www.glfw.org/download.html)                                 |
| assimp     | 6.0.5                  | **需要自己用 CMake 编译** | `Dependence/include/assimp`、`Dependence/lib/assimp` | [assimp/assimp](https://github.com/assimp/assimp/releases/tag/v6.0.5)          |
| Vulkan SDK | 1.4.357.0              | 安装包                | `C:\VulkanSDK\1.4.357.0`                            | [LunarG](https://vulkan.lunarg.com/sdk/home)                                   |


标"固定"的两个库不能随便升级，升级会导致编译失败，原因见[需要配合修改的代码](#需要配合修改的代码和配置)。

## 放好之后的目录结构

```
T_Core/vender/
├── imgui/        imgui.h imgui.cpp imgui_*.cpp imconfig.h imstb_*.h
│                 imgui_impl_glfw.*  imgui_impl_opengl3.*  imgui_impl_opengl3_loader.h  imgui_impl_vulkan.*
├── glm/          glm.hpp  detail/  ext/  gtc/  gtx/  simd/ ...      (没有 glm.cppm)
├── spdlog/       spdlog.h  sinks/  fmt/  details/ ...
├── stb_image/    stb_image.h  stb_image.cpp
└── entt/         entt.hpp

Dependence/
├── include/
│   ├── GL/       glew.h  wglew.h  eglew.h  glxew.h
│   ├── GLFW/     glfw3.h  glfw3native.h
│   └── assimp/   Importer.hpp  scene.h ...  config.h  revision.h  Compiler/  port/
└── lib/
    ├── GLEW/     glew32.lib  glew32s.lib  glew32.dll
    ├── GLFW/     glfw3.lib  (glfw3dll.lib  glfw3_mt.lib  glfw3.dll 用不到，放着无妨)
    └── assimp/   assimp-vc143-mtd.lib  assimp-vc143-mtd.dll  (assimp-vc143-mtd.pdb)
```

代码里的写法是 `#include "imgui/imgui.h"`、`<glm/glm.hpp>`、`"spdlog/spdlog.h"`、`"stb_image/stb_image.h"`、`<entt/entt.hpp>`，
include 根目录是 `T_Core/vender/` 和 `Dependence/include/`，所以**每个库都要平铺在自己的目录下，不能多一层**。

## 各库放置方式



### ImGui（源码，docking 分支）

- 必须用 **docking 分支**：代码用了 `ImGuiConfigFlags_DockingEnable` / `ViewportsEnable` / `DockSpace`。
- 拷根目录的所有 `*.h`、`*.cpp`，再从 `backends/` 拷下面几个文件，**和核心文件平铺放在一起**：
  - `imgui_impl_glfw.h/.cpp`
  - `imgui_impl_opengl3.h/.cpp`、`imgui_impl_opengl3_loader.h`
  - `imgui_impl_vulkan.h/.cpp`
- 其他 backend（dx、sdl、win32、allegro、android……）不要拷。`T_Core/premake5.lua` 的 `removefiles` 只排除了一部分，
多拷的 backend 会被编译，然后因为找不到对应的头文件报错。



### glm（纯头文件）

- 只拷仓库里的**内层** `glm/` 目录的内容到 `vender/glm/`（保证 `vender/glm/glm.hpp` 存在）。
- **删掉** `glm.cppm`，原因见下文。



### spdlog（纯头文件）

- 只拷 `include/spdlog/` 的内容到 `vender/spdlog/`（保证 `vender/spdlog/spdlog.h` 存在）。
- 用 header-only 模式，不需要编译 spdlog，也不要拷 `src/`。



### stb_image（单头文件）

- 拷 `stb_image.h`，再新建 `stb_image.cpp`：
  ```cpp
  #define STB_IMAGE_IMPLEMENTATION
  #include "stb_image.h"
  ```



### EnTT（单头文件）

- 拷 `single_include/entt/entt.hpp` 到 `vender/entt/entt.hpp`。
- **不要拿 master 分支的**：master 是 4.0 开发版，要求 C++20，项目是 C++17。



### GLEW（预编译）

- 下载 `glew-2.3.1-win32.zip`（包名叫 win32，里面同时有 Win32 和 x64）。
- `include/GL/*` → `Dependence/include/GL/`
- `lib/Release/x64/*`、`bin/Release/x64/glew32.dll` → `Dependence/lib/GLEW/`
- **只能用 x64 目录下的文件**，拿成 Win32 的运行时会报 `0xc000007b`。



### GLFW（预编译）

- 下载 `glfw-3.5.1.bin.WIN64.zip`（**WIN64**，不是 WIN32）。
- `include/GLFW/*` → `Dependence/include/GLFW/`
- `lib-vc2022/*` → `Dependence/lib/GLFW/`
- 项目链接的是静态库 `glfw3.lib`，不需要 `glfw3.dll`。



### assimp（需要自己编译）

assimp 没有官方的 Windows 预编译包，必须用 CMake 编。要求：**x64、Debug、动态库、VS2022（v143）**，
产物名必须是 `assimp-vc143-mtd`（premake 里写死了这个名字）。

**命令行（推荐，脚本** `-BuildAssimp` **做的就是这些）：**

```bat
git clone --depth 1 --branch v6.0.5 https://github.com/assimp/assimp.git F:\assimp
cd F:\assimp
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DBUILD_SHARED_LIBS=ON -DUSE_STATIC_CRT=OFF -DASSIMP_BUILD_ZLIB=ON ^
      -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF ^
      -DASSIMP_INSTALL=OFF -DASSIMP_WARNINGS_AS_ERRORS=OFF
cmake --build build --config Debug --parallel
```

没有单独装 CMake 的话，用 VS 自带的：
`C:\Program Files\Microsoft Visual Studio\2022\<版本>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`

**CMake GUI 的话（README 里的截图流程）：**

1. Source 选 assimp 目录，Build 选 `<assimp>/build`，Configure。
2. 生成器选 **Visual Studio 17 2022**，平台选 **x64**（不是 Win32）。
3. 勾选 `ASSIMP_BUILD_ZLIB`（不勾的话可能链接到 Anaconda 等环境里架构不对的 `z.lib`，编译失败）。
4. 建议取消 `ASSIMP_BUILD_TESTS`、`ASSIMP_WARNINGS_AS_ERRORS`。
5. Generate → Open Project → 选 `Debug | x64` 生成 `assimp` 项目。

**编好之后要拷两部分头文件：**


| 从                                                                | 到                            |
| ---------------------------------------------------------------- | ---------------------------- |
| `<assimp>/include/assimp/*`（不要 `*.in` 文件）                        | `Dependence/include/assimp/` |
| `<assimp>/build/include/assimp/config.h`、`revision.h`（CMake 生成的） | `Dependence/include/assimp/` |
| `<assimp>/build/lib/Debug/assimp-vc143-mtd.lib`                  | `Dependence/lib/assimp/`     |
| `<assimp>/build/bin/Debug/assimp-vc143-mtd.dll`（和 `.pdb`）        | `Dependence/lib/assimp/`     |


> 只看 build 目录会以为头文件很少，这是正常的：公开头文件都在**源码目录**的 `include/assimp`，build 目录里只有生成的 `config.h` 和 `revision.h`。

项目只用到了 assimp 的核心 API（`Assimp::Importer`、`ReadFile`、`aiProcess_*`、`aiScene/aiNode/aiMesh/aiFace`），
5.x 到 6.x 没有变化，用 master 编也可以，但建议用正式版本号保证可复现。

### Vulkan SDK

- 安装到 `C:\VulkanSDK\1.4.357.0`（安装包会自动设置 `VULKAN_SDK` 并把 `Bin` 加进 PATH）。
- 静默安装：`vulkansdk-windows-X64-1.4.357.0.exe --root C:\VulkanSDK\1.4.357.0 --accept-licenses --default-answer --confirm-command install`（需要管理员权限）。
- **OpenGL 后端也需要**：premake 在两个后端下都会链接 `vulkan-1.lib`、`shaderc_shared.lib`，
而且 `imgui_impl_vulkan.cpp` 总会被编译进 `T_Core.dll`。
- 运行时：`vulkan-1.dll` 由显卡驱动提供；Vulkan 后端还需要 `shaderc_shared.dll`，它在 SDK 的 `Bin` 里，靠 PATH 找到。



## 需要配合修改的代码和配置


| #   | 内容                                          | 状态                |
| --- | ------------------------------------------- | ----------------- |
| 1   | `vender/stb_image/stb_image.cpp` 提供 stb 的实现 | 需要新建（脚本会自动生成）     |
| 2   | 删除 `vender/glm/glm.cppm`                    | 需要删除（脚本自动处理）      |
| 3   | ImGui 从 `T_Core.dll` 导出                     | 已写在 premake 里，不用管 |
| 4   | ImGui 版本固定在 1.92.3                          | 升级前必须改代码          |
| 5   | EnTT 版本固定在 3.15.0                           | 升级前必须改代码          |
| 6   | Vulkan SDK 版本写死在 premake 里                  | 换 SDK 版本时要改       |
| 7   | assimp 库名写死为 `assimp-vc143-mtd`             | 换工具集时要改           |


**1. stb_image.cpp**：工程里没有任何地方定义 `STB_IMAGE_IMPLEMENTATION`，没有这个文件会在链接时报 `stbi_load` 等符号找不到。

**2. glm.cppm**：这是 glm 的 C++20 模块文件。++`T_Core/premake5.lua` ++用++ `"vender/**.**"` ++把 vender 下所有文件加进工程，++
`removefiles` ++里的++ `"*.cppm"` ++只匹配项目根目录，排除不到++ `vender/glm/glm.cppm`++，在 C++17 下会编译失败。

**3. IMGUI_API**：ImGui 被编译进 `T_Core.dll`，SandBox 要调用 `ImGui::Begin` 等函数，DLL 必须导出这些符号。
已经在 premake 里定义好：

- `T_Core/premake5.lua`：`"IMGUI_API=__declspec(dllexport)"`
- `SandBox/premake5.lua`：`"IMGUI_API=__declspec(dllimport)"`

所以 `imconfig.h` **保持官方原样就行，不要再在里面定义** `IMGUI_API`，否则会重定义。
漏了这个的话，SandBox 链接时会报几十个 `LNK2019: 无法解析的外部符号 ImGui::...`。

**4. ImGui 升级到 ≥ 1.92.4**：官方把 `ImGui_ImplVulkan_InitInfo` 的 `RenderPass`、`Subpass`、`MSAASamples`
挪进了 `PipelineInfoMain`，最新版已经把旧字段彻底删掉。升级时要改 `T_Core/src/Panels/Platform/Vulkan/ImGUIRenderVulkan.cpp`：

```cpp
// 1.92.3 及以前
initInfo.RenderPass  = context->GetImGuiRenderPass();
initInfo.Subpass     = 0;
initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

// 1.92.4 及以后
initInfo.PipelineInfoMain.RenderPass  = context->GetImGuiRenderPass();
initInfo.PipelineInfoMain.Subpass     = 0;
initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
```

另外代码用到了 `ImTextureID_Invalid` 和 `initInfo.ApiVersion`，所以也不能低于 1.92.0。

**5. EnTT 升级到 ≥ 3.16**：`entt::meta<T>()` 被删掉了，要改 `T_Core/src/Panels/ComponentInspectors.h`：

```cpp
// 3.15 及以前
entt::meta<T>().type(entt::hashed_string{ name });
// 3.16 及以后
entt::meta_factory<T>{}.type(entt::hashed_string{ name });
```

EnTT 4.x（master）要求 C++20，需要把 premake 的++ `cppdialect` ++改成 `C++20`，改动面更大。

**6. Vulkan SDK 路径**：`T_Core/premake5.lua` 和 `SandBox/premake5.lua` 的 `includedirs`、`libdirs` 里都写死了
`C:/VulkanSDK/1.4.357.0`，装了别的版本要同时改这 4 处（或者改成 `os.getenv("VULKAN_SDK")`）。
`setup_deps.ps1` 顶部的 `$V.VulkanSDK` 也要一起改。

**7. assimp 库名**：premake 的 `links`、`linkoptions` 和 `postbuildcommands` 里都写死了 `assimp-vc143-mtd`。
用 VS2026 等其他工具集编出来会叫 `vc145` 之类的名字，需要同步修改。Release 配置链接的也是这个 Debug 库，所以只编 Debug 就够了。

## 其他说明

- **运行库**：项目用 `/MDd`。官方的 `glfw3.lib` 是 `/MD` 编的，链接时可能出现 `LNK4098` 警告，不影响使用。assimp 按上面的参数编出来就是 `/MDd`，是匹配的。
- **DLL 拷贝**：`T_Core` 的 post-build 会把 `glew32.dll`、`assimp-vc143-mtd.dll` 拷到 `bin/`。GLFW 是静态链接，不需要 dll。
- **不要**把第三方库提交进 git：`.gitignore` 已经忽略了 `T_Core/vender/`*、`Dependence/*`，只保留 `.gitkeep`。



## 常见问题


| 现象                                             | 原因 / 解决                                                                   |
| ---------------------------------------------- | ------------------------------------------------------------------------- |
| 启动报 `0xc000007b`                               | 混进了 x86（Win32）的 dll，或者 `bin/` 里缺 dll。检查 `glew32.dll` 和 assimp dll 是不是 x64 |
| `entt.hpp` 报 `bit_ceil`、`same_as` 不是 `std` 的成员 | EnTT 版本是 4.x（C++20），换成 3.15.0                                             |
| `initInfo.RenderPass` 不是成员                     | ImGui 版本 ≥ 1.92.4，见第 4 条                                                  |
| `LNK2019 ImGui::Begin ...`                     | `IMGUI_API` 没有导出，见第 3 条（重新跑一次 `premake5 vs2022`）                          |
| `LNK2019 stbi_load`                            | 缺 `stb_image.cpp`                                                         |
| 找不到 `vulkan/vulkan.h` 或 `vulkan-1.lib`         | Vulkan SDK 没装，或者版本和 premake 里写的不一致                                        |
| assimp 编译时报 zlib 相关错误                          | 勾选 `ASSIMP_BUILD_ZLIB=ON`，用 assimp 自带的 zlib                               |
| 找不到 `assimp/config.h`                          | 只拷了源码目录的头文件，漏了 build 目录里生成的 `config.h`、`revision.h`                       |


