#include "ShaderParser.h"
#include "Debug/Debug.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace
{
    // Check whether a GLSL type belongs in a UBO (opaque types like samplers
    // and storage images are *not* UBO members — they go to their own binding).
    bool IsUBOCompatible(const std::string& glslType)
    {
        if (glslType.find("sampler") != std::string::npos) return false;
        if (glslType == "image2D" || glslType == "imageCube") return false;
        if (glslType == "atomic_uint") return false;
        return true;
    }

    // Strip a trailing `// @xxx` hint from a line (returns the trimmed line).
    // We only need to peek at hints, not preserve them in the GLSL output.
    std::string StripTrailingComment(const std::string& line)
    {
        auto pos = line.find("//");
        return pos == std::string::npos ? line : line.substr(0, pos);
    }

    // Parse a `// @binding N` annotation if the line carries it.
    bool TryReadBindingHint(const std::string& line, uint32_t& outBinding)
    {
        auto pos = line.find("@binding");
        if (pos == std::string::npos) return false;
        auto eq = line.find_first_of(" \t=", pos);
        if (eq == std::string::npos) return false;
        auto numStart = line.find_first_of("0123456789", eq);
        if (numStart == std::string::npos) return false;
        outBinding = static_cast<uint32_t>(std::stoul(line.substr(numStart)));
        return true;
    }

    // Parse a `// @ubo` hint explicitly keeping the uniform inside the UBO
    // even if a future heuristic would have moved it out.
    bool HasUboHint(const std::string& line)
    {
        return line.find("@ubo") != std::string::npos;
    }
}

ParsedShader ParseShaderFile(const std::string& filepath)
{
    std::ifstream stream(filepath);
    if (!stream.is_open())
    {
        Error_Core("Failed to open shader file at path: " + filepath);
        return {};
    }

    enum class Section { NONE = -1, VERTEX = 0, FRAGMENT = 1, COMPUTE = 2 };

    std::string line;
    std::stringstream ss[3];
    Section section = Section::NONE;
    bool inSystemBlock = false;

    // Per-section prologue injection. Each entry is a line of GLSL to inject
    // just before the user's #version. We build the std140 UBO wrapper here.
    struct SectionPrologue
    {
        std::vector<std::string> uboLines;        // members of the per-pass UBO
        std::vector<std::string> bindingLines;    // direct `layout(binding=N) uniform samplerX ...;`
    };
    SectionPrologue prologues[3];

    // Track the last binding hint seen while walking the file (so a
    // `// @binding 2` on line N applies to the next uniform, not the previous).
    int pendingBinding[3] = { -1, -1, -1 };
    int pendingForceUbo[3] = { 0, 0, 0 };

    // Auto-assign sampler bindings: 10..63, per-section, in source order.
    int nextAutoBinding[3] = { 10, 10, 10 };

    // Auto-UBO injection is opt-in. The whole project's existing 14 shaders
    // still work with the legacy uniform path; opt each in one at a time
    // (TAA is the pilot). A file opts in with `// @auto_ubo on` anywhere in
    // the .shader file, and reverts to the default loose-uniform behavior
    // with `// @auto_ubo off`.
    bool autoUboRequested = false;
    bool autoUboExplicit  = false;

    ParsedShader result;

    // Remember the original filename for the UBO block name — keeps each
    // shader's block distinct when multiple shaders are linked into the same
    // program.
    auto baseName = filepath;
    auto slash = baseName.find_last_of("/\\");
    if (slash != std::string::npos) baseName = baseName.substr(slash + 1);
    auto dot = baseName.find_last_of('.');
    if (dot != std::string::npos) baseName = baseName.substr(0, dot);
    auto sanitize = [](std::string s) {
        for (auto& c : s) if (!isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
        return s;
    };
    std::string uboName = "PerPass_" + sanitize(baseName);

    auto finalizePrologue = [&](int idx, std::stringstream& out)
    {
        if (!autoUboRequested) return;
        for (auto& line : prologues[idx].bindingLines)
            out << line << "\n";
        if (!prologues[idx].uboLines.empty())
        {
            out << "layout(std140, binding = 0) uniform " << uboName << " {\n";
            for (auto& line : prologues[idx].uboLines)
                out << "    " << line << "\n";
            out << "};\n";
        }
    };

    while (std::getline(stream, line))
    {
        // Section markers reset per-section state and flush stage output.
        if (line.find("#shader") != std::string::npos)
        {
            // Flush prior section's prologue onto the per-section output.
            if (section != Section::NONE)
                finalizePrologue((int)section, ss[(int)section]);

            if (line.find("vertex") != std::string::npos)        section = Section::VERTEX;
            else if (line.find("fragment") != std::string::npos) section = Section::FRAGMENT;
            else if (line.find("compute") != std::string::npos)  section = Section::COMPUTE;
            continue;
        }
        else if (line.find("[Header") != std::string::npos)
        {
            std::string label;
            int index = (int)line.find("[Header") + 8;
            label = line.substr(index, line.length() - index - 2);
            result.uniforms.push_back({ label, "Head", 0, true });
            continue;
        }
        else if (line.find("[System]") != std::string::npos)
        {
            inSystemBlock = !inSystemBlock;
            continue;
        }
        else if (line.find("@auto_ubo") != std::string::npos)
        {
            autoUboExplicit  = true;
            autoUboRequested = (line.find("on") != std::string::npos);
            continue;
        }

        // Read binding/ubo hints attached to the *next* uniform declaration.
        // Hints are only honored in auto-UBO mode.
        if (autoUboRequested &&
            (line.find("@binding") != std::string::npos || line.find("@ubo") != std::string::npos))
        {
            if (section == Section::NONE) continue;
            uint32_t b = 0; bool hasB = TryReadBindingHint(line, b);
            int idx = (int)section;
            pendingBinding[idx]  = hasB ? (int)b : -1;
            pendingForceUbo[idx] = HasUboHint(line) ? 1 : 0;
            continue;
        }

        if (section == Section::NONE) continue;
        int idx = (int)section;

        // Forward GLSL source to the per-section stream (with hints stripped).
        // In auto-UBO mode, if this line is a uniform declaration we routed
        // to the prologue, emit a blank line instead so source line numbers
        // stay roughly aligned for GLSL error messages.
        std::string clean = StripTrailingComment(line);
        bool emittedUniform = false;
        if (autoUboRequested && !inSystemBlock)
        {
            int uidx = clean.find("uniform");
            if (uidx != std::string::npos)
            {
                int ep = uidx + 7;
                while (ep < (int)clean.size() && clean[ep] == ' ') ep++;
                int en = ep;
                while (en < (int)clean.size() && clean[en] != ' ' && clean[en] != ';') en++;
                std::string tn = clean.substr(ep, en - ep);
                if (tn != "Head")
                {
                    int ns = en; while (ns < (int)clean.size() && clean[ns] == ' ') ns++;
                    int ne = ns;
                    while (ne < (int)clean.size() && clean[ne] != ' ' && clean[ne] != ';' && clean[ne] != '[') ne++;
                    if (ne > ns) emittedUniform = true;
                }
            }
        }
        if (emittedUniform) ss[idx] << '\n';
        else                ss[idx] << clean << '\n';

        // Uniform reflection: pull a (type, name) pair out of the line, if any.
        int uidx = clean.find("uniform");
        if (uidx == std::string::npos) continue;

        int endpos = uidx + 7;
        while (endpos < (int)clean.size() && clean[endpos] == ' ') endpos++;
        int endname = endpos;
        while (endname < (int)clean.size() && clean[endname] != ' ' && clean[endname] != ';')
            endname++;
        std::string typeName = clean.substr(endpos, endname - endpos);

        int nameStart = endname;
        while (nameStart < (int)clean.size() && clean[nameStart] == ' ') nameStart++;
        int nameEnd = nameStart;
        while (nameEnd < (int)clean.size() && clean[nameEnd] != ' ' && clean[nameEnd] != ';' && clean[nameEnd] != '[')
            nameEnd++;
        std::string rawName = clean.substr(nameStart, nameEnd - nameStart);
        if (rawName.empty()) continue;

        if (inSystemBlock) continue;
        if (typeName == "Head") continue;

        Uniform u;
        u.Name = rawName;
        u.Type = typeName;
        u.inUBO = IsUBOCompatible(typeName);
        u.binding = 0;

        // Decide binding source: explicit @binding, then auto-assigned
        // sampler slot, then UBO-default.
        if (pendingBinding[idx] >= 0)
        {
            u.binding = (uint32_t)pendingBinding[idx];
            // Direct bindings are never UBO members, even for scalar types.
            u.inUBO = false;
        }
        else if (!u.inUBO)
        {
            // Samplers/storage images: auto-assign from 10 upward, unless
            // @ubo forced it in (legal for image1D etc. but unusual).
            if (pendingForceUbo[idx])
            {
                u.inUBO = true;
            }
            else
            {
                u.binding = (uint32_t)nextAutoBinding[idx]++;
                u.inUBO = false;
            }
        }

        // Record the binding directive or UBO member on the per-section
        // prologue so GLShader can prepend it before compilation.
        if (!u.inUBO)
        {
            prologues[idx].bindingLines.push_back(
                "layout(binding = " + std::to_string(u.binding) + ") " + clean);
        }
        else
        {
            prologues[idx].uboLines.push_back(clean);
        }

        result.uniforms.push_back(u);
        pendingBinding[idx]  = -1;
        pendingForceUbo[idx] = 0;
    }

    // Flush the last section.
    if (section != Section::NONE)
        finalizePrologue((int)section, ss[(int)section]);

    if (!ss[0].str().empty()) result.sources[ShaderStage::Vertex]   = ss[0].str();
    if (!ss[1].str().empty()) result.sources[ShaderStage::Fragment] = ss[1].str();
    if (!ss[2].str().empty()) result.sources[ShaderStage::Compute]  = ss[2].str();

    if (autoUboRequested)
    {
        for (int i = 0; i < 3; ++i)
        {
            for (auto& line : prologues[i].uboLines)
            {
                auto semi = line.find(';');
                std::string trimmed = line.substr(0, semi == std::string::npos ? line.size() : semi);
                auto sp = trimmed.find_last_of(" \t");
                std::string id = sp == std::string::npos ? trimmed : trimmed.substr(sp + 1);
                auto br = id.find('[');
                if (br != std::string::npos) id = id.substr(0, br);
                result.uboMemberNames.push_back(id);
            }
        }
        result.uboBlockName = uboName;
    }

    return result;
}
