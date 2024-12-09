project "T_UI"
    kind "StaticLib"
    language "c++"

    outputdir =
        "build/%{prj.name}/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

    targetdir("../" .. outputdir)

    files
    {
        "include/**.h", 
        "src/**.cpp"
    }


    includedirs
    {
        "../T_Core/vender",
        "include/",
        "../T_Tool/include",
        "../T_Core/src/Platform/GL"
    }

    filter "system:windows" --对特定的系统(windows、OS..)、配置(Debug/Release)、平台(x64、x86)的项目属性
    cppdialect "C++17"  --C++特性版本
    staticruntime "On"  
    systemversion "10.0.22621.0"   -- windows SKD版本

    defines
    {
        "T_PLATFORM_WINDOWS",
    }