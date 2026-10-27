# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Tsundere** is a C++17 rendering engine built on a four-layer architecture with pluggable graphics API backends (OpenGL 4.5 and Vulkan 1.3). The engine uses **Render Dependency Graph (RDG)** for automated render pipeline orchestration, **ECS** (EnTT) for scene management, and **ImGui** for editor UI. The codebase is on Windows x64 only.

## Build System & Commands

The project uses **Premake5** for build generation and **Visual Studio 2022** for compilation.

### Setup & Build

```bat
:: One-shot dependency setup (run from repo root)
setup_deps.bat -BuildAssimp -InstallVulkanSDK

:: Generate OpenGL backend (default)
premake5.exe vs2022

:: Generate Vulkan backend
premake5.exe vs2022 --renderer=vulkan

:: Compile in Visual Studio 2022
:: Select Debug | x64, then build
:: Outputs: bin/SandBox.exe, bin/T_Core.dll, bin/*.dll
```

Key points:
- Architecture is **x64 only**—all dependencies must be x64
- C++ dialect: **C++17** (C++20 modules disabled; see `premake5.lua` `removefiles` for `*.cppm`)
- Runtime: `/MDd` (Debug dynamic CRT)
- Windows SDK: 10.0.22621.0 (hardcoded in premake; update if installing different SDK)

### Dependency Management

External libraries are in `.gitignore`. Use `setup_deps.ps1` (or the batch wrapper) to fetch them:

- **Header-only libs**: ImGui (docking branch, 1.92.3), glm (1.0.3), spdlog (1.17.0), stb_image, EnTT (3.15.0)
- **Prebuilt binaries**: GLEW (2.3.1), GLFW (3.5.1)
- **Compiled (requires CMake)**: assimp (6.0.5) with `ASSIMP_BUILD_ZLIB=ON` to avoid zlib conflicts
- **System install**: Vulkan SDK 1.4.357.0 → `C:\VulkanSDK\1.4.357.0` (required even for OpenGL backend)

See `DEPENDENCIES.md` for detailed setup, version constraints, and FAQ. Note: ImGui and EnTT versions are pinned; see **Code Constraints** below.

## Code Architecture

### Four-Layer Abstraction

```
┌─ Layer 1: Application (SandBox)
│  └─ VulkanExampleLayer / ExampleLayer: viewport, camera interaction, RDG drive
│
├─ Layer 2: Scene & Pipeline (T_Core)
│  ├─ Scene: ECS registry (EnTT), entities (Transform, MeshRender, Light, Camera)
│  └─ Pipeline: RenderGraph (DAG), RenderGraphPass registry, RenderPassManager
│
├─ Layer 3: Hardware Abstraction (T_Core::Platform::RHI)
│  └─ RHICommandBuffer, RHIPipeline, RHITexture2D, RHIDescriptorSet, ShaderCompiler
│
└─ Layer 4: Graphics API (OpenGL 4.5 / Vulkan 1.3)
   ├─ OpenGL: GLCommandBuffer, GLPipeline (VAO-based)
   └─ Vulkan: VulkanCommandBuffer (dynamic rendering), VulkanPipeline, layout tracking
```

**Why this design?** Complete decoupling: Pass implementations use only `RenderGraphBuilder` + `RHICommandBuffer` and know nothing of OpenGL vs Vulkan differences (barring a few transition-era macros).

### RDG (Render Dependency Graph) - Core System

Located in `T_Core/src/Pipeline/RenderGraph.h`, `RenderGraphPass.h`.

**Two-phase execution:**

1. **Compile** (once at init, on resize, or when Passes toggle):
   - Collect reads/writes declarations via `RenderGraphPass::Setup(builder)`
   - Derive dependencies & memory barriers (RAW, WAW, WAR)
   - Kahn topological sort → `executionOrder_`
   - Compute resource lifetimes for transient allocation
   - Allocate GPU memory

2. **Execute** (every frame, zero-alloc):
   - Begin command buffer
   - For each Pass in `executionOrder_`: call `ExecutePass()`
   - Submit to GPU

**Key designs:**
- **RGTextureHandle / RGBufferHandle** with `generation` field: prevents use-after-free bugs if a Handle outlives a graph reset (Resize or Pass toggle).
- **RenderGraphBuilder** (Setup): only for declarations; no physical resource access.
- **RenderGraphContext** (Execute): decodes Handles to physical `RHITexture2D*`; forbids structural mutations.
- **Symbolic naming**: Passes register resources like `"Gbuffer.SceneColor"`; downstream Passes read by name, zero coupling.

**Adding a new Pass:**

1. Inherit `RenderGraphPass` in `T_Core/src/Pipeline/Passes/YourPass.h`
2. Implement `Setup(builder)` (declare reads/writes) and `Execute(ctx)` (record GPU work)
3. Use `REGISTER_RENDER_PASS("YourPass")` macro at file-end for auto-registration
4. Toggle in `RenderPassManager` UI at runtime (no code change needed)

### Module Structure

- **T_Core**: Rendering engine core
  - `src/Core/`: Base classes (Asset, Resource, Serializer)
  - `src/Scene/`: ECS, Entity, Transform, Camera, Light
  - `src/Pipeline/`: RenderGraph, RenderGraphPass, RenderPassManager, individual Passes
  - `src/Platform/RHI/`: Abstract rendering interface
  - `src/Platform/GL/`: OpenGL backend
  - `src/Platform/Vulkan/`: Vulkan backend
  - `src/Panels/`: ImGui editor panels and Pass UI
  - `src/DDGI/`, `src/Debug/`, `src/Trans/`: Specialized subsystems

- **T_Tool**: Utility library (string, format, memory helpers)
- **T_UI**: ImGui layout and panel infrastructure
- **SandBox**: Main application / editor executable

## Code Constraints & Version Dependencies

Read `DEPENDENCIES.md` section **"需要配合修改的代码和配置"** before upgrading any library.

### Pinned Versions (Breaking Changes on Upgrade)

| Item | Version | File Impact | What Breaks |
|------|---------|------------|------------|
| ImGui | 1.92.3 (docking) | `T_Core/src/Panels/Platform/Vulkan/ImGUIRenderVulkan.cpp` | ImGui ≥ 1.92.4 moved `RenderPass`/`Subpass`/`MSAASamples` into `PipelineInfoMain` |
| EnTT | 3.15.0 | `T_Core/src/Panels/ComponentInspectors.h` | EnTT ≥ 3.16 removed `entt::meta<T>()`; use `entt::meta_factory<T>{}` instead |
| Vulkan SDK | 1.4.357.0 | `T_Core/premake5.lua`, `SandBox/premake5.lua` (hardcoded paths × 6) | Update path string in both premake files + `setup_deps.ps1` |
| assimp | 6.0.5 + vc143 | premake `links` and `linkoptions` | assimp lib name embeds toolset (vc143 = VS2022); update if using different VS version |

### glm.cppm Removal

After extracting glm 1.0.3 to `T_Core/vender/glm/`, **delete `glm.cppm`**. Premake's `removefiles` only matches root `*.cppm`; nested ones cause C++17 parse errors.

### Shader Compilation

Both backends compile shaders via `T_Core/src/Platform/ShaderCompiler.cpp`:
- **Input**: GLSL source
- **Compile**: shaderc → SPIR-V
- **Reflect**: SPIRV-Cross C API extracts descriptor bindings & push constants
- **GL backend**: SPIRV-Cross transpiles SPIR-V → GLSL 430

Both backends require Vulkan SDK (even GL backend for shader tools).

## Common Development Tasks

### Add a Rendering Pass

1. Create `T_Core/src/Pipeline/Passes/MyPass.h`/`.cpp`
2. Inherit `RenderGraphPass`; override `Setup()` and `Execute()`
3. In `Setup()`: declare your inputs/outputs via `builder.ReadTexture()`, `builder.WriteTexture()`, etc.
4. In `Execute()`: get physical resources via `ctx.GetTexture(handle)`, bind them, record draws
5. Macro-register at EOF: `REGISTER_RENDER_PASS("MyPass")`
6. Optionally add UI panel in `T_Core/src/Panels/` to toggle or configure it

Example setup pattern:
```cpp
void MyPass::Setup(RenderGraphBuilder& builder) {
  auto [tex] = builder.ReadTextures<"Input">();
  builder.WriteTexture("Output", ...) // creates "MyPass.Output"
}

void MyPass::Execute(RenderGraphContext& ctx) {
  auto* inputTex = ctx.GetTexture(m_InputHandle);
  auto* cmdBuf = ctx.GetCommandBuffer();
  // record GPU work
}
```

### Switch Between OpenGL & Vulkan Backends

```bat
:: Regenerate solution for the other backend
premake5.exe vs2022 --renderer=vulkan
:: OR
premake5.exe vs2022  # default OpenGL

:: Rebuild in VS 2022
```

Conditional compilation handles both via `#ifdef Vulkan_For_Render` and `#ifdef OpenGL_For_Render`.

### Edit Shaders

Shaders are compiled at runtime by `ShaderCompiler.cpp`. Modify `.glsl` files in the asset directories; rerun the app to recompile. No shader cache persistence yet (always recompile on startup).

### Debug Resource Barriers & Layout Transitions

- **Vulkan**: Enable validation layers and check `vkCmdPipelineBarrier` insertions in `RenderGraph::Execute()`
- **OpenGL**: Insert `glMemoryBarrier` calls via `RenderGraph`'s barrier tracking (not yet exposed to UI)

## Platform-Specific Notes

### Windows Environment

- **Must**: Visual Studio 2022 (v143 toolset), Windows 10/11 x64
- **Post-build**: DLLs (glew32, assimp-vc143-mtd, shaderc_shared, spirv-cross-c-shared) auto-copied to `bin/` by T_Core's postbuildcommands
- **Runtime**: Launch `bin/SandBox.exe`

### OpenGL

- Uses core profile 4.5
- VAO-based pipeline
- ImGui impl: `imgui_impl_opengl3`
- Include path setup: `src/Platform/GL/` in premake (for loader headers)

### Vulkan

- Uses dynamic rendering (vkCmdBeginRendering, no render pass objects)
- Automatic image layout tracking in `VulkanTexture2D`
- ImGui impl: `imgui_impl_vulkan`
- SDK requirements: Both dyn libs (shaderc_shared, spirv-cross-c-shared) and headers

## Testing & Validation

No automated test suite yet. Manual validation:
1. Build both backends: `premake5.exe vs2022`, then `premake5.exe vs2022 --renderer=vulkan`
2. Run SandBox.exe for each, verify viewport renders, UI responds, model loading works
3. Toggle Passes in RenderPassManager UI; check no crashes or artifact corruption

## Repository Branches & Workflow

- **master**: stable
- **Sandbox** (current dev): active development branch
- Workflow: commit to Sandbox, test, merge to master when stable
- Git user: iqifa

## File Organization Notes

- **Source committed**: All `.h`, `.cpp` in `T_Core/src`, `SandBox/src`, `T_Tool/include`, `T_UI/include`
- **NOT committed**: Dependencies (`T_Core/vender/*`, `Dependence/*`), build artifacts (`build/`, `bin/*.dll` except critical ones), object files
- **Imgui State**: `imgui.ini` is in `.gitignore` but sits at repo root; regenerated per session
