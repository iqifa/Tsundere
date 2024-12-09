project "SandBox"
    kind "ConsoleApp"
    language "c++"

    outputdir =
        "build/%{prj.name}/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

    targetdir("../" .. outputdir)

    files
    {
        "src/**.h", 
        "src/**.cpp"
    }


    includedirs
    {
        "../T_Core/vender",
        "../T_Core/src",
        "../T_Core/src/Core",
        "../T_Core/src/Platform/GL",
        "../T_Tool/include",
        "../T_UI/include",
        -- "../T_Core/vender/spdlog/include",
        "../Dependence/include",
    }

    libdirs{
        "../Dependence/lib/GLFW/",
        "../Dependence/lib/GLEW/",
        "../Dependence/lib/assimp/",
        "../bin"
    }

    filter "system:windows" --对特定的系统(windows、OS..)、配置(Debug/Release)、平台(x64、x86)的项目属性
    cppdialect "C++17"  --C++特性版本
    staticruntime "On"  
    systemversion "10.0.22621.0"    -- windows SKD版本


    linkoptions{
        "glfw3.lib","opengl32.lib","glew32.lib","assimp-vc143-mtd.lib","T_Core.lib"
    }

    filter{
        "system:windows",
        "action:vs2022",
        buildoptions{"/utf-8","/MDd"}
    }
    defines
    {
        "T_PLATFORM_WINDOWS",
    }