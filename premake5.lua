solution "Tsundere"
architecture "x64"
configurations {"Debug","Release"}

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

targetdir ("build/%{cfg.buildcfg}")


dofile("T_Core/premake5.lua")
dofile("T_Tool/premake5.lua")
dofile("T_UI/premake5.lua")
dofile("SandBox/premake5.lua")