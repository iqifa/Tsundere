-- premake5-defs.lua
-- Environment detection shared by all premake projects: the actual assimp
-- import-library name (which embeds the toolset version, e.g. vc143) and the
-- installed Vulkan SDK. Do NOT hardcode either in the per-project premake
-- files -- they change with the machine (new VS / new SDK).
--
-- Usage (top of premake5.lua, before dofile of the sub-projects):
--   dofile("premake5-defs.lua")
--
-- ASSIMP_LIB is the base name WITHOUT extension (e.g. "assimp-vc143-mtd"):
-- append ".lib" to link and ".dll" to copy commands.

local function find_assimp_lib()
    -- Match assimp-*-mtd.lib in Dependence/lib/assimp (any toolset suffix).
    local matches = os.matchfiles("Dependence/lib/assimp/assimp-*-mtd.lib")
    if #matches == 0 then
        return nil
    end
    -- Deterministic pick if several toolsets are installed side by side.
    table.sort(matches)
    local full = matches[#matches]                -- e.g. Dependence/lib/assimp/assimp-vc143-mtd.lib
    local name = full:match("([^/\\]+)$")         -- assimp-vc143-mtd.lib
    return name:sub(1, -5)                        -- strip ".lib" -> assimp-vc143-mtd
end

local function find_vulkan_sdk()
    -- 1) Premake does not inherit the VS environment block reliably, but the
    --    VULKAN_SDK env var is set machine-wide by the LunarG installer.
    local root = os.getenv("VULKAN_SDK")
    -- 2) Fallback: newest version directory under C:/VulkanSDK.
    if not root or root == "" or not os.isdir(root) then
        local best = nil
        local dirs = os.matchdirs("C:/VulkanSDK/*")
        for _, d in ipairs(dirs) do
            -- Keep only numeric-looking version dirs, prefer the highest.
            local ver = d:match("([%d%.]+)$")
            if ver and (not best or ver > best) then best = ver end
        end
        if best then root = "C:/VulkanSDK/" .. best end
    end
    if root and os.isdir(root .. "/Lib") and os.isdir(root .. "/Include") then
        return root
    end
    return nil
end

local function find_windows_sdk_version()
    -- 1) The VS developer environment sets WindowsSdkDir; the version is the
    --    directory name directly above /Include.
    local dir = os.getenv("WindowsSdkDir")
    local ver = os.getenv("WindowsSDKVersion")   -- e.g. "10.0.22621.0\"
    if ver then ver = ver:match("([%d%.]+)") end
    if dir and os.isdir(dir .. "/Include") and ver and os.isdir(dir .. "/Include/" .. ver) then
        return ver
    end
    -- 2) Fallback: newest numeric version under the standard Kits location.
    local kits = "C:/Program Files (x86)/Windows Kits/10/Include"
    if not os.isdir(kits) then return nil end
    local best = nil
    for _, d in ipairs(os.matchdirs(kits .. "/*")) do
        local v = d:match("([%d%.]+)$")
        if v and (not best or v > best) then best = v end
    end
    return best
end

ASSIMP_LIB = find_assimp_lib()
VULKAN_SDK_ROOT = find_vulkan_sdk()
WINSDK_VERSION = find_windows_sdk_version()

-- Fail with a readable message instead of a cryptic linker error later.
if not ASSIMP_LIB then
    print("")
    print("WARNING: no assimp import library found in Dependence/lib/assimp.")
    print("         Run setup_deps.bat -BuildAssimp first; linking will fail until then.")
    print("")
end
if not VULKAN_SDK_ROOT then
    print("")
    print("WARNING: no Vulkan SDK found (checked VULKAN_SDK and C:/VulkanSDK/*).")
    print("         Run setup_deps.bat -InstallVulkanSDK first; linking will fail until then.")
    print("")
end
if not WINSDK_VERSION then
    print("")
    print("WARNING: no Windows 10/11 SDK found (checked WindowsSdkDir and Windows Kits/10/Include).")
    print("         Install it via the VS2022 installer; project generation will fail until then.")
    print("")
end

if ASSIMP_LIB then
    print("premake: assimp library   = " .. ASSIMP_LIB)
end
if VULKAN_SDK_ROOT then
    print("premake: Vulkan SDK root  = " .. VULKAN_SDK_ROOT)
end
if WINSDK_VERSION then
    print("premake: Windows SDK ver  = " .. WINSDK_VERSION)
end
