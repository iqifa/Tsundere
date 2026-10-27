# Material System Refactor - Migration Guide

This document describes the material system refactor completed and the manual integration steps required.

## What Was Implemented

### Phase 1: New Material Core System ✅

**New Files:**
- `T_Core/src/Core/Material.h` + `.cpp`: New Material class with:
  - Type-safe `MaterialParameter` using `std::variant`
  - Automatic parameter extraction from `ShaderReflection`
  - Serialization/Deserialization to `.tmat` text format
  - Convenience getters/setters for all types

- `T_Core/src/Core/MaterialLibrary.h` + `.cpp`: Material asset management
  - `Load(path)`, `Save(material, path)`, `Get(path)` (cached)
  - `CreateDefault(name, shader)`

**Removed:**
- `T_Core/src/Panels/Material.h` (old Material class with unsafe `uintptr_t` pointers)
- `T_Core/src/Panels/TextureImportPanel.h` (replaced by AssetsPanel)

**Modified:**
- `T_Core/src/Platform/RHI/RHIShader.h`:
  - Removed `Uniform` struct (line 13-25)
  - Deprecated `GetUniforms()` method (commented out, line 48)
  - Shader reflection now uses only `ShaderReflection` from `RHIShaderReflection.h`

### Phase 2: Unified Assets Panel ✅

**New Files:**
- `T_Core/src/Panels/AssetsPanel.h` + `.cpp`: Unified asset browser
  - Left panel: Folder tree navigation
  - Right panel: Grid/list view with thumbnails
  - Supported types: Models, Textures, Materials (.tmat), Shaders
  - Features: Import, Delete, Rename, Show in Explorer, Open
  - Double-click Model → opens ModelEditorPanel
  - Double-click Material → opens MaterialEditorPanel (TODO: needs implementation)

### Phase 3: Entity Material Inspector Panel ✅

**New Files:**
- `T_Core/src/Panels/MaterialInspectorPanel.h`: Inline inspector function
  - `RenderMaterialInspector(Entity& entity)`: Call this from your main inspector
  - Dynamically generates UI from `ShaderReflection`
  - Per-mesh material editing
  - Save/Load `.tmat` files
  - Texture browsing and assignment

**Modified:**
- `T_Core/src/Panels/ComponentInspectors.h`:
  - Added `#include "Panels/MaterialInspectorPanel.h"`
  - Old `RenderMeshRenderInspector` still exists but needs to be replaced/updated

### Phase 4: Shader Reflection Integration ✅

- Old `Uniform` struct removed from `RHIShader.h`
- New `Material::InitializeParametersFromShader()` extracts parameters from:
  - `ShaderReflection::UniformBlocks` → Float/Vec2/Vec3/Vec4/Int parameters
  - `ShaderReflection::SampledImages` → Texture2D parameters

## Manual Integration Required

### 1. Update Premake Build Script

Add new files to `T_Core/premake5.lua`:

```lua
-- Already included by "src/**.cpp" pattern:
-- T_Core/src/Core/Material.cpp
-- T_Core/src/Core/MaterialLibrary.cpp
-- T_Core/src/Panels/AssetsPanel.cpp

-- If you use explicit file lists, add these files
```

### 2. Replace Old MeshRenderInspector

In `T_Core/src/Panels/ComponentInspectors.h`, find the function `RenderMeshRenderInspector(Entity& entity)` (around line 301).

**Option A: Full Replacement**

Replace the entire function body with a simplified version that uses the new MaterialInspectorPanel:

```cpp
inline void RenderMeshRenderInspector(Entity& entity)
{
    if (!entity.HasComponent<MeshRender>())
        return;

    auto& meshrender = entity.GetComponent<MeshRender>();

    ImGui::PushID("##MeshRender");

    bool open = ImGui::CollapsingHeader("MeshRender",
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);

    float btnW = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btnW);
    if (ImGui::SmallButton("-"))
    {
        entity.RemoveComponent<MeshRender>();
        BVHBuilder::MarkActiveDirty();
        ImGui::PopID();
        return;
    }

    if (open)
    {
        // Model selection
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::Text("Model");
        ImGui::NextColumn();

        std::string preview = meshrender.ModelPath.empty()
            ? "<none>"
            : meshrender.ModelPath.substr(meshrender.ModelPath.find_last_of("/\\") + 1);

        if (ImGui::BeginCombo("##ModelPath", preview.c_str()))
        {
            for (const auto& path : My_map::GetModelPaths())
            {
                bool selected = (path == meshrender.ModelPath);
                std::string display = path.substr(path.find_last_of("/\\") + 1);
                if (ImGui::Selectable(display.c_str(), selected))
                {
                    meshrender.ModelPath = path;
                    BVHBuilder::MarkActiveDirty();
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("..."))
        {
            std::string importPath = OpenModelFileDialog();
            if (!importPath.empty())
            {
                My_map::AddModelPath(importPath);
                Engine::ResourceLoader::RequestModelLoad(importPath);
                meshrender.ModelPath = importPath;
                BVHBuilder::MarkActiveDirty();
            }
        }
        ImGui::Columns(1);
    }

    ImGui::PopID();

    // Render material inspector (NEW!)
    RenderMaterialInspector(entity);
}
```

**Option B: Keep Old Code, Add New Panel**

If you want to preserve the old material editing UI temporarily, just add this line after the existing `RenderMeshRenderInspector` closes:

```cpp
// At the end of your inspector panel rendering:
RenderMaterialInspector(entity);
```

### 3. Remove Old Material Widget Code

In `ComponentInspectors.h`, delete or comment out these sections:

- `struct MeshRenderInspectorState` (line ~195)
- `inline MeshRenderInspectorState s_MeshRenderState` (line ~204)
- `void ClearMaterialWidgets()` (line ~206)
- `void BuildMaterialWidgets(Ref<Material> mat)` (line ~213)

These are no longer needed with the new system.

### 4. Integrate AssetsPanel into Main Application

In your main `ExampleLayer` or `VulkanExampleLayer` (in `SandBox/`), add the AssetsPanel:

```cpp
#include "Panels/AssetsPanel.h"

// In your OnAttach() or constructor:
auto* assetsPanel = new AssetsPanel("Assets");
AssetsPanel::BindScene(m_Scene); // Bind your scene
Engine::Application::Get().PushLayer(assetsPanel);
```

### 5. Update GetUniforms() Calls

Search for all calls to `shader->GetUniforms()` and replace them with `shader->GetReflection()`:

**Before:**
```cpp
const std::vector<Uniform>& uniforms = shader->GetUniforms();
for (const auto& uniform : uniforms) {
    // Process uniform
}
```

**After:**
```cpp
const ShaderReflection& refl = shader->GetReflection();
for (const auto& block : refl.UniformBlocks) {
    for (const auto& member : block.Members) {
        // Process member.Name, member.Type, member.Offset, member.Size
    }
}
for (const auto& sampler : refl.SampledImages) {
    // Process sampler.Name, sampler.Type, sampler.Binding
}
```

### 6. Update Old Material References

Replace any remaining uses of the old `Material` class:

**Before:**
```cpp
Ref<Material> mat = CreateRef<Material>("res/shaders/Lamber.shader");
mat->InitVaires(); // Old API
mat->Render(); // Old API
```

**After:**
```cpp
Ref<Engine::Material> mat = Engine::MaterialLibrary::CreateDefault(
    "MyMaterial",
    ShaderLibiray::Get("res/shaders/Lamber.shader")
);
mat->SetVec3("albedo", glm::vec3(0.8f, 0.2f, 0.1f));
mat->SetFloat("roughness", 0.5f);
mat->SetTexture("baseColorMap", TextureLibiary::Get("res/textures/wood.png"));
```

### 7. Implement Material Binding in Rendering Passes

In your RDG passes (e.g., `GeometryPassV2`, `DeferredLightPass`), update material binding:

**Before:**
```cpp
meshRender.materials[i]->Render(shader);
```

**After:**
```cpp
Ref<Engine::Material> mat = meshRender.materials[i];

// Bind textures
for (const auto& [name, param] : mat->GetParameters()) {
    if (param.type == Engine::MaterialParameter::Type::Texture2D) {
        Ref<RHITexture2D> tex = mat->GetTexture(name);
        if (tex) {
            descriptorSet->BindSampledTexture(binding, tex); // Use correct binding from reflection
        }
    }
}

// Upload uniform parameters to UBO (example)
struct MaterialUBO {
    glm::vec3 albedo;
    float roughness;
    glm::vec3 emissive;
    float metallic;
};

MaterialUBO ubo;
ubo.albedo = mat->GetVec3("albedo", glm::vec3(1.0f));
ubo.roughness = mat->GetFloat("roughness", 0.5f);
ubo.emissive = mat->GetVec3("emissive", glm::vec3(0.0f));
ubo.metallic = mat->GetFloat("metallic", 0.0f);

materialUboBuffer->Upload(&ubo, sizeof(ubo));
descriptorSet->BindUniformBuffer(1, materialUboBuffer);
```

### 8. Test and Verify

1. Build both OpenGL and Vulkan backends:
   ```bat
   premake5.exe vs2022
   premake5.exe vs2022 --renderer=vulkan
   ```

2. Test AssetsPanel:
   - Browse folders
   - Import a texture
   - Double-click a model to open ModelEditorPanel

3. Test MaterialInspectorPanel:
   - Select an entity with MeshRender
   - Edit material parameters (color, roughness, etc.)
   - Assign a texture
   - Save material to `.tmat` file
   - Load material from `.tmat` file

4. Verify no crashes when switching backends

## Material File Format (.tmat)

Example `res/materials/wood.tmat`:

```
# Tsundere Material File
Name: Wood Material
Shader: res/shaders/PBR.shader

[Parameters]
albedo = vec3 0.8 0.5 0.3
roughness = float 0.7
metallic = float 0.0
baseColorMap = texture2d res/textures/wood_albedo.png
normalMap = texture2d res/textures/wood_normal.png
```

## Known Limitations & Future Work

1. **MaterialEditorPanel**: Not yet implemented. Currently materials can only be edited through EntityInspector when assigned to MeshRender. A standalone editor panel is needed for creating/editing materials in AssetsPanel.

2. **Thumbnail Rendering**: Assets Panel displays texture thumbnails but doesn't render material/model previews. Future: add thumbnail generation for materials and models.

3. **Backward Compatibility**: Old scenes with MeshRender components using the old Material class need migration. Add a migration script or handle it in deserialization.

4. **Shader Hot-Reload**: When shaders are recompiled, materials need to refresh their parameters automatically.

5. **Binary Serialization**: Current text format is human-readable but slower. Consider adding binary `.tmat` format for production builds.

6. **Material Instances**: No support for material instancing yet. All materials are unique copies.

## Questions or Issues?

If you encounter compilation errors or runtime issues:

1. Check that all new files are included in `premake5.lua`
2. Verify `#include "Core/Material.h"` paths are correct
3. Ensure `Engine::` namespace prefix is used for new Material classes
4. Check that old `Material.h` (in Panels/) is not being included anywhere

For any blockers, refer to the plan file:
`C:\Users\ADMIN\.claude\plans\sandbox-1-2-modeltexture-3-entity-panel-dazzling-lampson.md`
