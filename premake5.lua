solution "Tsundere"
architecture "x64"
configurations {"Debug","Release"}

filter { "system:windows", "action:vs*" }
    buildoptions { "/utf-8" }
filter {}

newoption
{
    trigger = "renderer",
    value = "BACKEND",
    description = "Select the rendering backend",
    allowed = {
        { "opengl", "OpenGL renderer" },
        { "vulkan", "Vulkan renderer" }
    }
}

RenderBackend = _OPTIONS["renderer"] or "opengl"

if RenderBackend ~= "opengl" and RenderBackend ~= "vulkan" then
    error("Invalid renderer: " .. RenderBackend .. ". Use --renderer=opengl or --renderer=vulkan")
end

-- Detect the assimp library name and Vulkan SDK location for THIS machine
-- (see premake5-defs.lua). Sets the globals ASSIMP_LIB / VULKAN_SDK_ROOT.
dofile("premake5-defs.lua")

targetdir ("build/%{cfg.buildcfg}")


dofile("T_Core/premake5.lua")
dofile("T_Tool/premake5.lua")
dofile("T_UI/premake5.lua")
dofile("SandBox/premake5.lua")
