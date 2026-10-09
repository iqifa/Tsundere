#include "ShaderCompiler.h"
#include "Debug/Debug.h"

#include <shaderc/shaderc.h>
#include <spirv_cross/spirv_cross_c.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

namespace
{
    constexpr uint32_t kFirstAutoTextureBinding = 10;
    constexpr uint32_t kGLSLVersion             = 430;

    constexpr std::array<ShaderStage, 3> kSectionStages = {
        ShaderStage::Vertex, ShaderStage::Fragment, ShaderStage::Compute
    };

    const char* StageName(ShaderStage stage)
    {
        switch (stage)
        {
        case ShaderStage::Vertex:   return "vertex";
        case ShaderStage::Fragment: return "fragment";
        case ShaderStage::Compute:  return "compute";
        case ShaderStage::Geometry: return "geometry";
        }
        return "unknown";
    }

    bool IsOpaqueType(const std::string& glslType)
    {
        return glslType.find("sampler") != std::string::npos
            || glslType.find("image") != std::string::npos;
    }

    // =========================================================================
    // .shader file splitting + editor metadata
    // =========================================================================

    struct DeclaredUniform
    {
        std::string Name;              // header label when IsHeader
        std::string Type;
        bool        System          = false;
        bool        ExplicitBinding = false;
        bool        IsHeader        = false;
    };

    struct ShaderSourceFile
    {
        std::vector<ShaderStage>                     Stages;     // file order
        std::unordered_map<ShaderStage, std::string> Sections;   // same line count as the file
        std::vector<DeclaredUniform>                 Declared;   // source order, headers interleaved
    };

    std::string Trim(const std::string& s)
    {
        const auto first = s.find_first_not_of(" \t\r");
        if (first == std::string::npos) return {};
        const auto last = s.find_last_not_of(" \t\r");
        return s.substr(first, last - first + 1);
    }

    // Every section keeps the file's full line count (foreign lines blanked) so
    // compiler diagnostics report line numbers of the .shader file itself.
    bool LoadShaderSource(const std::string& filepath, ShaderSourceFile& out)
    {
        std::ifstream stream(filepath);
        if (!stream.is_open())
        {
            Error_Core("Failed to open shader file at path: {}", filepath);
            return false;
        }

        static const std::regex kUniformDecl(R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?(\w+)\s+(\w+))");
        static const std::regex kBindingQualifier(R"(\bbinding\s*=)");

        std::array<std::vector<std::string>, kSectionStages.size()> lines;
        int current = -1;
        bool inSystem = false;
        std::vector<std::string> pendingHeaders;
        std::unordered_set<std::string> seenUniforms;

        auto flushHeaders = [&]() {
            for (auto& label : pendingHeaders)
                out.Declared.push_back({ label, "Head", false, false, true });
            pendingHeaders.clear();
        };

        std::string line;
        while (std::getline(stream, line))
        {
            bool marker = true;
            if (line.find("#shader") != std::string::npos)
            {
                current = -1;
                for (size_t i = 0; i < kSectionStages.size(); ++i)
                {
                    if (line.find(StageName(kSectionStages[i])) != std::string::npos)
                    {
                        current = static_cast<int>(i);
                        out.Stages.push_back(kSectionStages[i]);
                        break;
                    }
                }
            }
            else if (line.find("[System]") != std::string::npos)
            {
                inSystem = !inSystem;
            }
            else if (const auto h = line.find("[Header"); h != std::string::npos)
            {
                const auto close = line.find(']', h);
                pendingHeaders.push_back(Trim(line.substr(h + 7, close == std::string::npos ? std::string::npos : close - h - 7)));
            }
            else
            {
                marker = false;
            }

            for (size_t i = 0; i < lines.size(); ++i)
                lines[i].push_back(!marker && static_cast<int>(i) == current ? line : std::string());

            if (marker || current < 0)
                continue;

            const std::string code = line.substr(0, line.find("//"));
            std::smatch m;
            if (!std::regex_search(code, m, kUniformDecl))
                continue;

            flushHeaders();
            const std::string name = m[2].str();
            if (!seenUniforms.insert(name).second)
                continue;
            out.Declared.push_back({ name, m[1].str(), inSystem, std::regex_search(code, kBindingQualifier), false });
        }
        flushHeaders();

        for (ShaderStage stage : out.Stages)
        {
            size_t idx = 0;
            while (kSectionStages[idx] != stage) ++idx;

            std::string joined;
            for (const auto& l : lines[idx])
            {
                joined += l;
                joined += '\n';
            }
            out.Sections[stage] = std::move(joined);
        }
        return true;
    }

    // `#include` needs GL_GOOGLE_include_directive; the `#line` keeps later
    // diagnostics on the original line numbers.
    std::string EnableIncludeDirective(const std::string& section)
    {
        if (section.find("#include") == std::string::npos)
            return section;

        std::istringstream in(section);
        std::ostringstream out;
        std::string line;
        int lineNumber = 0;
        bool injected = false;
        while (std::getline(in, line))
        {
            ++lineNumber;
            out << line << '\n';
            if (!injected && Trim(line).rfind("#version", 0) == 0)
            {
                out << "#extension GL_GOOGLE_include_directive : require\n";
                out << "#line " << (lineNumber + 1) << '\n';
                injected = true;
            }
        }
        return out.str();
    }

    // =========================================================================
    // shaderc
    // =========================================================================

    struct IncludeFile
    {
        std::string            Name;
        std::string            Content;
        shaderc_include_result Result{};
    };

    shaderc_include_result* ResolveInclude(void*, const char* requested, int,
                                           const char* requesting, size_t)
    {
        auto* file = new IncludeFile;
        const std::filesystem::path path =
            (std::filesystem::path(requesting).parent_path() / requested).lexically_normal();

        std::ifstream in(path, std::ios::binary);
        if (in)
        {
            std::ostringstream content;
            content << in.rdbuf();
            file->Name    = path.generic_string();
            file->Content = content.str();
        }
        else
        {
            file->Content = "cannot open include file: " + path.generic_string();
        }

        file->Result = { file->Name.c_str(), file->Name.size(),
                         file->Content.c_str(), file->Content.size(), file };
        return &file->Result;
    }

    void ReleaseInclude(void*, shaderc_include_result* result)
    {
        delete static_cast<IncludeFile*>(result->user_data);
    }

    shaderc_shader_kind ToShadercKind(ShaderStage stage)
    {
        switch (stage)
        {
        case ShaderStage::Vertex:   return shaderc_glsl_vertex_shader;
        case ShaderStage::Fragment: return shaderc_glsl_fragment_shader;
        case ShaderStage::Compute:  return shaderc_glsl_compute_shader;
        case ShaderStage::Geometry: return shaderc_glsl_geometry_shader;
        }
        return shaderc_glsl_vertex_shader;
    }

    bool CompileToSpirv(shaderc_compiler_t compiler, ShaderStage stage, const std::string& source,
                        const std::string& filepath, ShaderTarget target, std::vector<uint32_t>& out)
    {
        shaderc_compile_options_t options = shaderc_compile_options_initialize();
        if (target == ShaderTarget::OpenGL)
        {
            // layout(push_constant) is rejected by the OpenGL shaderc client.
            // Compile that stage as Vulkan SPIR-V; EmitGLSL lowers the block to a
            // default-block uniform (`uniform DrawPush pc`).
            const bool hasPushConstant = source.find("push_constant") != std::string::npos;
            if (hasPushConstant)
                shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
            else
                shaderc_compile_options_set_target_env(options, shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);
            // SPIR-V requires a location on every varying and loose uniform. OpenGL
            // sources rely on name matching instead; FixupForOpenGL restores that.
            shaderc_compile_options_set_auto_map_locations(options, true);
            shaderc_compile_options_add_macro_definition(options, "TS_OPENGL", 9, "1", 1);
        }
        else
        {
            shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
            shaderc_compile_options_add_macro_definition(options, "TS_VULKAN", 9, "1", 1);
        }
        shaderc_compile_options_set_include_callbacks(options, ResolveInclude, ReleaseInclude, nullptr);

        const std::string prepared = EnableIncludeDirective(source);
        shaderc_compilation_result_t result = shaderc_compile_into_spv(
            compiler, prepared.c_str(), prepared.size(), ToShadercKind(stage),
            filepath.c_str(), "main", options);

        const bool ok = shaderc_result_get_compilation_status(result) == shaderc_compilation_status_success;
        if (!ok)
        {
            Error_Core("Shader compile error [{}] ({} stage):\n{}", filepath, StageName(stage),
                       shaderc_result_get_error_message(result));
        }
        else
        {
            if (shaderc_result_get_num_warnings(result) > 0)
                Warn_Core("Shader compile warnings [{}] ({} stage):\n{}", filepath, StageName(stage),
                          shaderc_result_get_error_message(result));

            const size_t bytes = shaderc_result_get_length(result);
            out.resize(bytes / sizeof(uint32_t));
            std::memcpy(out.data(), shaderc_result_get_bytes(result), bytes);
        }

        shaderc_result_release(result);
        shaderc_compile_options_release(options);
        return ok;
    }

    // =========================================================================
    // SPIRV-Cross reflection
    // =========================================================================

    struct SpvcContextDeleter
    {
        void operator()(spvc_context_s* ctx) const { spvc_context_destroy(ctx); }
    };
    using SpvcContextPtr = std::unique_ptr<spvc_context_s, SpvcContextDeleter>;

    void OnSpvcError(void*, const char* error)
    {
        Error_Core("SPIRV-Cross: {}", error);
    }

    struct StageModule
    {
        ShaderStage   Stage;
        spvc_compiler Compiler  = nullptr;
        spvc_resources Resources = nullptr;
    };

    using ResourceList = std::pair<const spvc_reflected_resource*, size_t>;

    ResourceList GetResources(const StageModule& module, spvc_resource_type type)
    {
        const spvc_reflected_resource* list = nullptr;
        size_t count = 0;
        spvc_resources_get_resource_list_for_type(module.Resources, type, &list, &count);
        return { list, count };
    }

    std::string ImageTypeName(spvc_compiler compiler, spvc_type type, bool storage)
    {
        std::string name;
        spvc_type sampled = spvc_compiler_get_type_handle(compiler, spvc_type_get_image_sampled_type(type));
        switch (spvc_type_get_basetype(sampled))
        {
        case SPVC_BASETYPE_INT32:  name = "i"; break;
        case SPVC_BASETYPE_UINT32: name = "u"; break;
        default: break;
        }

        name += storage ? "image" : "sampler";
        switch (spvc_type_get_image_dimension(type))
        {
        case SpvDim1D:     name += "1D"; break;
        case SpvDim3D:     name += "3D"; break;
        case SpvDimCube:   name += "Cube"; break;
        case SpvDimRect:   name += "2DRect"; break;
        case SpvDimBuffer: name += "Buffer"; break;
        default:           name += "2D"; break;
        }
        if (spvc_type_get_image_multisampled(type)) name += "MS";
        if (spvc_type_get_image_arrayed(type))      name += "Array";
        if (!storage && spvc_type_get_image_is_depth(type)) name += "Shadow";
        return name;
    }

    std::string TypeName(spvc_compiler compiler, spvc_type_id id)
    {
        spvc_type type = spvc_compiler_get_type_handle(compiler, id);
        const unsigned vec  = spvc_type_get_vector_size(type);
        const unsigned cols = spvc_type_get_columns(type);

        auto matrixName = [&](const char* prefix) {
            return std::string(prefix) + std::to_string(cols)
                + (cols == vec ? std::string() : "x" + std::to_string(vec));
        };

        std::string name;
        switch (spvc_type_get_basetype(type))
        {
        case SPVC_BASETYPE_BOOLEAN: name = vec > 1 ? "bvec" + std::to_string(vec) : "bool"; break;
        case SPVC_BASETYPE_INT32:   name = vec > 1 ? "ivec" + std::to_string(vec) : "int"; break;
        case SPVC_BASETYPE_UINT32:  name = vec > 1 ? "uvec" + std::to_string(vec) : "uint"; break;
        case SPVC_BASETYPE_FP32:
            name = cols > 1 ? matrixName("mat") : (vec > 1 ? "vec" + std::to_string(vec) : "float");
            break;
        case SPVC_BASETYPE_FP64:
            name = cols > 1 ? matrixName("dmat") : (vec > 1 ? "dvec" + std::to_string(vec) : "double");
            break;
        case SPVC_BASETYPE_STRUCT:
            name = spvc_compiler_get_name(compiler, spvc_type_get_base_type_id(type));
            break;
        case SPVC_BASETYPE_SAMPLED_IMAGE: name = ImageTypeName(compiler, type, false); break;
        case SPVC_BASETYPE_IMAGE:         name = ImageTypeName(compiler, type, true); break;
        default:                          name = "unknown"; break;
        }

        const unsigned dims = spvc_type_get_num_array_dimensions(type);
        for (unsigned i = 0; i < dims; ++i)
        {
            const SpvId size = spvc_type_get_array_dimension(type, i);
            const bool literal = spvc_type_array_dimension_is_literal(type, i);
            name += (literal && size > 0) ? "[" + std::to_string(size) + "]" : "[]";
        }
        return name;
    }

    ShaderBlock ReflectBlock(spvc_compiler compiler, const spvc_reflected_resource& res)
    {
        ShaderBlock block;
        block.Name    = res.name ? res.name : "";
        block.Set     = spvc_compiler_get_decoration(compiler, res.id, SpvDecorationDescriptorSet);
        block.Binding = spvc_compiler_get_decoration(compiler, res.id, SpvDecorationBinding);

        spvc_type type = spvc_compiler_get_type_handle(compiler, res.base_type_id);
        size_t size = 0;
        spvc_compiler_get_declared_struct_size(compiler, type, &size);
        block.Size = static_cast<uint32_t>(size);

        const unsigned count = spvc_type_get_num_member_types(type);
        for (unsigned i = 0; i < count; ++i)
        {
            const spvc_type_id memberId = spvc_type_get_member_type(type, i);

            ShaderBlockMember member;
            const char* memberName = spvc_compiler_get_member_name(compiler, res.base_type_id, i);
            member.Name = memberName ? memberName : "";
            member.Type = TypeName(compiler, memberId);

            unsigned offset = 0;
            spvc_compiler_type_struct_member_offset(compiler, type, i, &offset);
            member.Offset = offset;

            size_t memberSize = 0;
            spvc_compiler_get_declared_struct_member_size(compiler, type, i, &memberSize);
            member.Size = static_cast<uint32_t>(memberSize);

            if (spvc_type_get_num_array_dimensions(spvc_compiler_get_type_handle(compiler, memberId)) > 0)
            {
                unsigned stride = 0;
                spvc_compiler_type_struct_member_array_stride(compiler, type, i, &stride);
                member.ArrayStride = stride;
            }
            block.Members.push_back(std::move(member));
        }
        return block;
    }

    ShaderImageBinding ReflectImage(spvc_compiler compiler, const spvc_reflected_resource& res)
    {
        ShaderImageBinding image;
        image.Name    = res.name ? res.name : "";
        image.Type    = TypeName(compiler, res.base_type_id);
        image.Set     = spvc_compiler_get_decoration(compiler, res.id, SpvDecorationDescriptorSet);
        image.Binding = spvc_compiler_get_decoration(compiler, res.id, SpvDecorationBinding);
        return image;
    }

    template<typename T>
    void AppendUnique(std::vector<T>& dst, T item)
    {
        for (const auto& existing : dst)
            if (existing.Set == item.Set && existing.Binding == item.Binding && existing.Name == item.Name)
                return;
        dst.push_back(std::move(item));
    }

    void ReflectModule(const StageModule& module, ShaderReflection& out)
    {
        auto [ubos, uboCount] = GetResources(module, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER);
        for (size_t i = 0; i < uboCount; ++i)
            AppendUnique(out.UniformBlocks, ReflectBlock(module.Compiler, ubos[i]));

        auto [ssbos, ssboCount] = GetResources(module, SPVC_RESOURCE_TYPE_STORAGE_BUFFER);
        for (size_t i = 0; i < ssboCount; ++i)
            AppendUnique(out.StorageBlocks, ReflectBlock(module.Compiler, ssbos[i]));

        auto [sampled, sampledCount] = GetResources(module, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE);
        for (size_t i = 0; i < sampledCount; ++i)
            AppendUnique(out.SampledImages, ReflectImage(module.Compiler, sampled[i]));

        auto [storage, storageCount] = GetResources(module, SPVC_RESOURCE_TYPE_STORAGE_IMAGE);
        for (size_t i = 0; i < storageCount; ++i)
            AppendUnique(out.StorageImages, ReflectImage(module.Compiler, storage[i]));
    }

    // =========================================================================
    // OpenGL fixups (applied before SPIRV-Cross emits GLSL)
    // =========================================================================

    class BindingAllocator
    {
    public:
        explicit BindingAllocator(uint32_t first) : m_Next(first) {}

        void Reserve(uint32_t binding) { m_Used.insert(binding); }

        uint32_t Get(const std::string& name)
        {
            if (auto it = m_ByName.find(name); it != m_ByName.end())
                return it->second;
            while (m_Used.count(m_Next)) ++m_Next;
            m_Used.insert(m_Next);
            return m_ByName[name] = m_Next++;
        }

    private:
        uint32_t m_Next;
        std::set<uint32_t> m_Used;
        std::unordered_map<std::string, uint32_t> m_ByName;
    };

    // OpenGL resolves uniform locations and texture units per program, while
    // shaderc assigns them per stage. Undo the per-stage choices:
    //   - loose uniforms / samplers / images: drop Location, the driver assigns it by name
    //   - samplers / images without an explicit binding: allocate one per name
    //   - fragment inputs: take the location of the vertex output with the same name
    void FixupForOpenGL(std::vector<StageModule>& modules, const ShaderSourceFile& file)
    {
        std::unordered_set<std::string> explicitBinding;
        for (const auto& d : file.Declared)
            if (d.ExplicitBinding)
                explicitBinding.insert(d.Name);

        BindingAllocator textures(kFirstAutoTextureBinding);
        BindingAllocator images(0);

        for (const auto& module : modules)
        {
            for (auto [type, allocator] : { std::pair{ SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &textures },
                                            std::pair{ SPVC_RESOURCE_TYPE_STORAGE_IMAGE, &images } })
            {
                auto [list, count] = GetResources(module, type);
                for (size_t i = 0; i < count; ++i)
                    if (explicitBinding.count(list[i].name))
                        allocator->Reserve(spvc_compiler_get_decoration(module.Compiler, list[i].id, SpvDecorationBinding));
            }
        }

        std::unordered_map<std::string, unsigned> vertexOutputs;
        for (const auto& module : modules)
        {
            if (module.Stage != ShaderStage::Vertex) continue;
            auto [outputs, count] = GetResources(module, SPVC_RESOURCE_TYPE_STAGE_OUTPUT);
            for (size_t i = 0; i < count; ++i)
                vertexOutputs[outputs[i].name] = spvc_compiler_get_decoration(module.Compiler, outputs[i].id, SpvDecorationLocation);
        }

        for (auto& module : modules)
        {
            auto [plain, plainCount] = GetResources(module, SPVC_RESOURCE_TYPE_GL_PLAIN_UNIFORM);
            for (size_t i = 0; i < plainCount; ++i)
                spvc_compiler_unset_decoration(module.Compiler, plain[i].id, SpvDecorationLocation);

            for (auto [type, allocator] : { std::pair{ SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &textures },
                                            std::pair{ SPVC_RESOURCE_TYPE_STORAGE_IMAGE, &images } })
            {
                auto [list, count] = GetResources(module, type);
                for (size_t i = 0; i < count; ++i)
                {
                    spvc_compiler_unset_decoration(module.Compiler, list[i].id, SpvDecorationLocation);
                    if (!explicitBinding.count(list[i].name))
                        spvc_compiler_set_decoration(module.Compiler, list[i].id, SpvDecorationBinding,
                                                     allocator->Get(list[i].name));
                }
            }

            if (module.Stage != ShaderStage::Fragment) continue;
            auto [inputs, inputCount] = GetResources(module, SPVC_RESOURCE_TYPE_STAGE_INPUT);
            for (size_t i = 0; i < inputCount; ++i)
            {
                auto it = vertexOutputs.find(inputs[i].name);
                if (it != vertexOutputs.end())
                    spvc_compiler_set_decoration(module.Compiler, inputs[i].id, SpvDecorationLocation, it->second);
            }
        }
    }

    bool EmitGLSL(const StageModule& module, std::string& out)
    {
        spvc_compiler_options options = nullptr;
        spvc_compiler_create_compiler_options(module.Compiler, &options);
        spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_GLSL_VERSION, kGLSLVersion);
        spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_ES, SPVC_FALSE);
        spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_VULKAN_SEMANTICS, SPVC_FALSE);
        // Keep push constants as `uniform DrawPush pc`, not a UBO. glUniform
        // addresses the members as "pc.model" and "pc.prevModel".
        spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_EMIT_PUSH_CONSTANT_AS_UNIFORM_BUFFER, SPVC_FALSE);
        spvc_compiler_install_compiler_options(module.Compiler, options);

        const char* source = nullptr;
        if (spvc_compiler_compile(module.Compiler, &source) != SPVC_SUCCESS || !source)
            return false;
        out = source;
        return true;
    }

    // =========================================================================
    // Editor uniform list
    // =========================================================================

    struct ReflectedUniform
    {
        std::string Type;
        uint32_t    Binding = 0;
        bool        Opaque  = false;
    };

    std::vector<Uniform> BuildEditorUniforms(const ShaderSourceFile& file,
                                             const std::vector<StageModule>& modules)
    {
        std::unordered_map<std::string, ReflectedUniform> reflected;
        for (const auto& module : modules)
        {
            auto [plain, plainCount] = GetResources(module, SPVC_RESOURCE_TYPE_GL_PLAIN_UNIFORM);
            for (size_t i = 0; i < plainCount; ++i)
                reflected.emplace(plain[i].name, ReflectedUniform{ TypeName(module.Compiler, plain[i].base_type_id), 0, false });

            for (auto type : { SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, SPVC_RESOURCE_TYPE_STORAGE_IMAGE })
            {
                auto [list, count] = GetResources(module, type);
                for (size_t i = 0; i < count; ++i)
                    reflected.emplace(list[i].name, ReflectedUniform{
                        TypeName(module.Compiler, list[i].base_type_id),
                        spvc_compiler_get_decoration(module.Compiler, list[i].id, SpvDecorationBinding),
                        true });
            }
        }

        std::vector<Uniform> uniforms;
        for (const auto& d : file.Declared)
        {
            if (d.IsHeader)
            {
                uniforms.push_back({ d.Name, "Head", 0, true });
                continue;
            }
            if (d.System)
                continue;

            if (auto it = reflected.find(d.Name); it != reflected.end())
                uniforms.push_back({ d.Name, it->second.Type, it->second.Binding, !it->second.Opaque });
            else if (modules.empty())
                uniforms.push_back({ d.Name, d.Type, 0, !IsOpaqueType(d.Type) });
        }
        return uniforms;
    }
}

CompiledShader CompileShaderFile(const std::string& filepath, ShaderTarget target)
{
    CompiledShader result;

    ShaderSourceFile file;
    if (!LoadShaderSource(filepath, file))
        return result;

    bool bypassSpirv = false;
    if (target == ShaderTarget::OpenGL)
    {
        for (const auto& [stage, source] : file.Sections)
            if (source.find("GL_ARB_bindless_texture") != std::string::npos)
                bypassSpirv = true;
    }

    if (bypassSpirv)
    {
        result.Glsl     = file.Sections;
        result.Uniforms = BuildEditorUniforms(file, {});
        result.Success  = true;
        return result;
    }

    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    bool ok = true;
    for (ShaderStage stage : file.Stages)
    {
        std::vector<uint32_t> spirv;
        if (CompileToSpirv(compiler, stage, file.Sections[stage], filepath, target, spirv))
            result.Spirv[stage] = std::move(spirv);
        else
            ok = false;
    }
    shaderc_compiler_release(compiler);
    if (!ok)
        return result;

    spvc_context rawContext = nullptr;
    if (spvc_context_create(&rawContext) != SPVC_SUCCESS)
    {
        Error_Core("SPIRV-Cross: failed to create context for {}", filepath);
        return result;
    }
    SpvcContextPtr context(rawContext);
    spvc_context_set_error_callback(rawContext, OnSpvcError, nullptr);

    const spvc_backend backend = target == ShaderTarget::OpenGL ? SPVC_BACKEND_GLSL : SPVC_BACKEND_NONE;

    std::vector<StageModule> modules;
    for (ShaderStage stage : file.Stages)
    {
        const auto& spirv = result.Spirv[stage];

        StageModule module{ stage };
        spvc_parsed_ir ir = nullptr;
        if (spvc_context_parse_spirv(rawContext, spirv.data(), spirv.size(), &ir) != SPVC_SUCCESS
            || spvc_context_create_compiler(rawContext, backend, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &module.Compiler) != SPVC_SUCCESS
            || spvc_compiler_create_shader_resources(module.Compiler, &module.Resources) != SPVC_SUCCESS)
        {
            Error_Core("SPIRV-Cross: failed to reflect {} ({} stage)", filepath, StageName(stage));
            return result;
        }
        modules.push_back(module);
    }

    if (target == ShaderTarget::OpenGL)
    {
        FixupForOpenGL(modules, file);
        for (const auto& module : modules)
        {
            std::string glsl;
            if (!EmitGLSL(module, glsl))
            {
                Error_Core("SPIRV-Cross: failed to emit GLSL for {} ({} stage)", filepath, StageName(module.Stage));
                return result;
            }
            result.Glsl[module.Stage] = std::move(glsl);
        }
    }

    for (const auto& module : modules)
        ReflectModule(module, result.Reflection);
    result.Uniforms = BuildEditorUniforms(file, modules);
    result.Success  = true;
    return result;
}
