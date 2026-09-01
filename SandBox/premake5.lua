project "SandBox"
    kind "ConsoleApp"
    language "c++"

    -- Exe goes straight into bin/ so it sits next to T_Core.dll / glew32.dll /
    -- assimp dll. Windows finds DLLs next to the exe, not in a separate dir.
    targetdir("../bin")

    files
    {
        "src/**.h", 
        "src/**.cpp"
    }
    removefiles
    {
    }


    includedirs
    {
        "../T_Core/vender",
        "../T_Core/src",
        "../T_Core/src/Core",
        "../T_Core/src/Platform/GL",
        "../T_Tool/include",
        "../T_UI/include",
        "C:/VulkanSDK/1.4.357.0/Include",
        -- "../T_Core/vender/spdlog/include",
        "../Dependence/include",
    }

    libdirs{
        "../Dependence/lib/GLFW/",
        "../Dependence/lib/GLEW/",
        "../Dependence/lib/assimp/",
        "C:/VulkanSDK/1.4.357.0/Lib",
        "../bin"
    }

    dependson{"T_Core"}
    filter "system:windows" --对特定的系统(windows、OS..)、配置(Debug/Release)、平台(x64、x86)的项目属性
    cppdialect "C++17"  --C++特性版本
    staticruntime "On"  
    systemversion "10.0.22621.0"    -- windows SKD版本


    linkoptions{
        "glfw3.lib","opengl32.lib","glew32.lib","assimp-vc143-mtd.lib","T_Core.lib","vulkan-1.lib","shaderc_shared.lib"
    }

    filter{
        "system:windows",
        "action:vs2022",
        buildoptions{"/utf-8","/MDd"}
    }
    defines
    {
        "T_PLATFORM_WINDOWS",
        RenderBackend == "vulkan" and "Vulkan_For_Render" or "OpenGL_For_Render"
    }