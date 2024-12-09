project "T_Core"
    kind "SharedLib"
    language "C++"
   
    outputdir = "build/%{prj.name}/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
    
    targetdir("../"..outputdir)

    files
    {
        "src/**.h",    
        "src/**.cpp",
        "vender/**.**"
    }
    removefiles{"src/Test/**.*","src/GL.cpp"}

    includedirs
    {
        "vender/",
        "../Dependence/include",
        "src/",
        "../T_Tool/include",
        "../T_UI/include",
        "src/Platform/GL",
        -- "vender/spdlog/include"
    }
    libdirs{
        "../Dependence/lib/GLFW/",
        "../Dependence/lib/GLEW/",
        "../Dependence/lib/assimp/",
    }
    filter "system:windows" --对特定的系统(windows、OS..)、配置(Debug/Release)、平台(x64、x86)的项目属性
    cppdialect "C++17"  --C++特性版本
    staticruntime "On"  
    systemversion "10.0.22621.0"    -- windows SKD版本

    links{
        "glfw3.lib","opengl32.lib","glew32.lib","assimp-vc143-mtd.lib"
    }


    filter{
        "system:windows",
        "action:vs2022",
        buildoptions{"/utf-8","/MDd"}
    }
    defines
    {
        "T_PLATFORM_WINDOWS",
        "T_BUILD_DLL"
    }


    postbuildcommands{
        ("{COPY} %{cfg.targetdir} ../bin")
    }

    