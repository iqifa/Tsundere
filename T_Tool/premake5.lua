project "T_Tool"
    kind "StaticLib"
    language "c++"

    outputdir =
        "build/%{prj.name}/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

    targetdir("../" .. outputdir)

    files
    {
        "include/**.h", 
        "include/**.inl",
        "src/**.cpp"
    }


    includedirs
    {
       "../vender/src",
       "../T_Core/include"
    }

    filter "system:windows" --对特定的系统(windows、OS..)、配置(Debug/Release)、平台(x64、x86)的项目属性
    cppdialect "C++17"  --C++特性版本
    staticruntime "On"  
    systemversion "10.0.22621.0"    -- windows SKD版本

    defines
    {
        "T_PLATFORM_WINDOWS",
    }