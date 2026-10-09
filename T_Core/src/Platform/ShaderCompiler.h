#pragma once

#include "RHI/RHIShader.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

enum class ShaderTarget : uint8_t
{
    OpenGL,
    Vulkan
};

struct CompiledShader
{
    bool Success = false;

    // Per-stage SPIR-V. Missing for OpenGL stages that bypass SPIR-V.
    std::unordered_map<ShaderStage, std::vector<uint32_t>> Spirv;

    // OpenGL only: per-stage GLSL ready for glShaderSource.
    std::unordered_map<ShaderStage, std::string> Glsl;

    ShaderReflection     Reflection;
    std::vector<Uniform> Uniforms;
};

// Compiles a multi-stage .shader file (`#shader vertex|fragment|compute` sections)
// with shaderc. `#include "path"` resolves relative to the including file, and
// TS_OPENGL / TS_VULKAN is defined for the active target.
//
// OpenGL: GLSL -> SPIR-V -> GLSL 430 via SPIRV-Cross. A stage that uses
// layout(push_constant) is compiled as Vulkan SPIR-V, then lowered to
// `uniform <Block> <instance>`. Sources using GL_ARB_bindless_texture cannot
// go through SPIR-V and are passed to the driver as written (no #include, no reflection).
CompiledShader CompileShaderFile(const std::string& filepath, ShaderTarget target);
