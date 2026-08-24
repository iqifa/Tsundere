#pragma once

#include "RHITypes.h"
#include "Core/Core.h"
#include "HeadLine.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Uniform metadata — parsed from shader source, drives the Material editor
// and the per-pass UBO builder.
//
// Binding rules (resolved at parse time, see ShaderParser.cpp):
//   - Default: every uniform is a member of the per-pass UBO at binding=0
//     (descriptor set 0, binding 0). GLShader auto-injects a std140 UBO
//     block wrapping all loose uniforms before compile, so .shader files
//     keep writing `uniform mat4 foo;` with no extra boilerplate.
//   - `// @binding <N>` directly above a `uniform` declaration pins that
//     uniform to binding N (e.g. a sampler binding directly).
//   - Samplers are auto-pinned to bindings 10..63 (one per sampler, in
//     source order) and excluded from the UBO. Override with @binding.
struct Uniform
{
    std::string Name;
    std::string Type;
    uint32_t    binding = 0;     // 0 = UBO member; N>0 = pinned binding
    bool        inUBO   = true;  // false when pinned to a non-UBO binding
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
