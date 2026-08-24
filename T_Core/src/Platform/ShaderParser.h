#pragma once

#include "RHI/RHITypes.h"
#include "RHI/RHIShader.h"    // for Uniform struct
#include <string>
#include <vector>
#include <unordered_map>

// Result of parsing a .shader file.
// Contains per-stage GLSL sources and extracted binding metadata.
//
// New: in addition to the per-uniform reflection (Uniform), the parser
// emits binding directives that the C++ side uses to lay out the per-pass
// UBO. See docs in RHIShader.h.
struct ParsedShader
{
    std::unordered_map<ShaderStage, std::string> sources;

    // Flat list of every uniform encountered. Each carries its resolved
    // binding (0 = member of the per-pass UBO; N>0 = pinned to binding N).
    std::vector<Uniform> uniforms;

    // Names of uniforms that became members of the auto-injected
    // `layout(std140, binding = 0) uniform PerPass_<name> { ... };` block,
    // in source order. The C++ side uses this list (plus std140 layout
    // rules) to build a matching struct.
    std::vector<std::string> uboMemberNames;
    std::string uboBlockName;
};

// Parse a .shader file into per-stage GLSL sources + binding metadata.
ParsedShader ParseShaderFile(const std::string& filepath);
