#pragma once

#include "RHI/RHITypes.h"
#include "RHI/RHIShader.h"    // for Uniform struct
#include <string>
#include <vector>
#include <unordered_map>

// Result of parsing a .shader file.
// Contains per-stage GLSL source and extracted uniform metadata.
struct ParsedShader
{
    std::unordered_map<ShaderStage, std::string> sources;
    std::vector<Uniform> uniforms;
};

// Parse a .shader file into per-stage GLSL sources + uniform metadata.
//
// File format (single file, multi-section):
//   #shader vertex
//   ... GLSL vertex source ...
//   #shader fragment
//   ... GLSL fragment source ...
//   #shader compute
//   ... GLSL compute source ...
//
// Special markers:
//   [Header <name>]  — adds a Uniform{name, "Head"} (UI section header)
//   [System]         — toggles exclusion of following uniforms from the list
//
// The returned GLSL sources are ready for backend compilation.
// The returned uniform list drives the Material editor UI.
ParsedShader ParseShaderFile(const std::string& filepath);
