#pragma once
#include <filesystem>
#include <string>

namespace Engine {
// Resolve references against the owning material, never the launch directory alone.
inline std::string ResolveMaterialResourcePath(const std::string& materialPath,
    const std::string& resourcePath)
{
    namespace fs = std::filesystem;
    if (resourcePath.empty()) return resourcePath;
    const fs::path requested(resourcePath);
    if (requested.is_absolute()) return requested.lexically_normal().string();
    const auto owner = fs::absolute(materialPath).parent_path();
    std::error_code ec;
    const auto local = owner / requested;
    if (fs::is_regular_file(local, ec)) return local.lexically_normal().string();
    for (auto directory = owner; !directory.empty(); )
    {
        if (fs::is_directory(directory / "Assets", ec) && fs::is_directory(directory / "res", ec))
        {
            const auto candidate = directory / requested;
            if (fs::is_regular_file(candidate, ec)) return candidate.lexically_normal().string();
            break;
        }
        const auto parent = directory.parent_path();
        if (parent == directory) break;
        directory = parent;
    }
    // Preserve legacy references outside the project and their original diagnostics.
    if (fs::is_regular_file(requested, ec)) return fs::absolute(requested).lexically_normal().string();
    return resourcePath;
}
}
