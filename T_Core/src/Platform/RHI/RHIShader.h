#pragma once

#include "RHITypes.h"
#include "RHIShaderReflection.h"
#include "Core/Core.h"
#include "HeadLine.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <shared_mutex>

// Editor-facing uniform list that drives the Material inspector, built by
// ShaderCompiler from SPIR-V reflection in source declaration order.
//   - Loose `uniform <type> <name>;` values (OpenGL only): inUBO = true, binding = 0.
//   - Samplers / storage images: inUBO = false, binding = resolved binding.
//     Samplers without an explicit `layout(binding = N)` get 10, 11, ... on OpenGL.
//   - `[Header <label>]` lines become Type == "Head" entries.
//   - Uniforms between `[System]` markers are engine-driven and omitted.
struct Uniform
{
    std::string Name;
    std::string Type;
    uint32_t    binding = 0;
    bool        inUBO   = true;
};

// Shader module interface.
// GL backend: wraps glCreateShader/glLinkProgram, stores GLSL source.
// VK backend: wraps vkCreateShaderModule from SPIR-V bytecode.
class T_API RHIShader
{
public:
    virtual ~RHIShader() = default;

    // Activate/deactivate the shader program
    virtual void Bind() const = 0;
    virtual void UnBind() const = 0;

    // Compute dispatch
    virtual void DispatchCompute(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) const = 0;

    // Identity
    virtual const std::string& GetName() const = 0;
    virtual const std::string& GetPath() const = 0;
    virtual uint32_t GetID() const = 0;

    // Parsed uniforms (for Material editor UI)
    virtual const std::vector<Uniform>& GetUniforms() const = 0;

    // Resource layout reflected from SPIR-V. Empty for OpenGL shaders that
    // bypass SPIR-V (e.g. GL_ARB_bindless_texture).
    virtual const ShaderReflection& GetReflection() const = 0;

    // --- Uniform setters (transitional — DescriptorSet will replace these in Chunk 4/5) ---
    virtual void SetUniform4f(const std::string& name, float v0, float v1, float v2, float v3) const = 0;
    virtual void SetUniform1f(const std::string& name, float value) const = 0;
    virtual void SetUniform1i(const std::string& name, int value) const = 0;
    virtual void SetUniformMat4f(const std::string& name, const glm::mat4& mat) const = 0;
    virtual void SetUniformVec3(const std::string& name, const glm::vec3& value) const = 0;
    virtual void SetUniformVec2(const std::string& name, const glm::vec2& value) const = 0;

    // --- Factories (defined inline in GL backend header) ---
    static Ref<RHIShader> Create(const std::string& filepath);
    static Ref<RHIShader> Create(const std::string& filepath, const std::string& name);
    static Ref<RHIShader> CreateCompute(const std::string& filepath);
};
class T_API ShaderLibiray {
public:

    static void Add(const Ref<RHIShader>& shader);
    static Ref<RHIShader> Load(const std::string& FilePath);
    static Ref<RHIShader> Load(const std::string& name, const std::string& FilePath);

    static Ref<RHIShader> Get(const std::string& path);

    static std::shared_mutex s_Mutex;
private:
    static std::unordered_map<std::string, Ref<RHIShader>>m_Shaders;
};

inline void ShaderLibiray::Add(const Ref<RHIShader>& shader)
{
    std::unique_lock lock(s_Mutex);
    auto& path = shader->GetPath();
    m_Shaders[path] = shader;
}

inline Ref<RHIShader> ShaderLibiray::Load(const std::string& FilePath)
{
    {
        std::shared_lock lock(s_Mutex);
        auto it = m_Shaders.find(FilePath);
        if (it != m_Shaders.end())
            return it->second;
    }
    auto shader = RHIShader::Create(FilePath);  // Ref<Shader> → Ref<RHIShader> implicit upcast
    {
        std::unique_lock lock(s_Mutex);
        m_Shaders[FilePath] = shader;
    }
    return shader;
}

inline Ref<RHIShader> ShaderLibiray::Load(const std::string& name, const std::string& FilePath)
{
    {
        std::shared_lock lock(s_Mutex);
        auto it = m_Shaders.find(FilePath);
        if (it != m_Shaders.end())
            return it->second;
    }
    auto shader = RHIShader::Create(FilePath, name);  // Ref<Shader> → Ref<RHIShader>
    {
        std::unique_lock lock(s_Mutex);
        m_Shaders[FilePath] = shader;
    }
    return shader;
}

inline Ref<RHIShader> ShaderLibiray::Get(const std::string& path)
{
    {
        std::shared_lock lock(s_Mutex);
        auto it = m_Shaders.find(path);
        if (it != m_Shaders.end())
            return it->second;
    }
    return Load(path);
}
