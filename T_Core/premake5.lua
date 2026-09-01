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
    removefiles
    {
        "src/Test/**.*",
        "src/GL.cpp",
        "*.cppm",
        "vender/imgui/imgui_impl_dx10.*",
        "vender/imgui/imgui_impl_dx11.*",
        "vender/imgui/imgui_impl_dx12.*",
        "vender/imgui/imgui_impl_dx9.*",
        "vender/imgui/imgui_impl_metal.*",
        "vender/imgui/imgui_impl_osx.*",
        "vender/imgui/imgui_impl_sdl2.*",
        "vender/imgui/imgui_impl_sdl3.*",
        "vender/imgui/imgui_impl_sdlgpu3.*",
        "vender/imgui/imgui_impl_sdlrenderer2.*",
        "vender/imgui/imgui_impl_sdlrenderer3.*",
        "vender/imgui/imgui_impl_wgpu.*",
        "vender/imgui/imgui_impl_win32.*"
    }

    if RenderBackend == "vulkan" then
        removefiles
        {
            "src/Platform/GL/**.h",
            "src/Platform/GL/**.cpp",
            "src/Platform/Windows/GLWindow.h",
            "src/Platform/Windows/GLWindow.cpp",
            "src/Panels/Platform/GL/**.h",
            "src/Panels/Platform/GL/**.cpp"
        }
    else
        removefiles
        {
            "src/Platform/Vulkan/**.h",
            "src/Platform/Vulkan/**.cpp",
            "src/Platform/Windows/VulkanWindow.h",
            "src/Platform/Windows/VulkanWindow.cpp",
            "src/Panels/Platform/Vulkan/**.h",
            "src/Panels/Platform/Vulkan/**.cpp"
        }
    end

    includedirs
    {
        "vender/",
        "../Dependence/include",
        "src/",
        "../T_Tool/include",
        "../T_UI/include",
        "src/Platform/GL",
        "C:/VulkanSDK/1.4.357.0/Include",
        -- "vender/spdlog/include"
    }
    libdirs{
        "../Dependence/lib/GLFW/",
        "../Dependence/lib/GLEW/",
        "../Dependence/lib/assimp/",
        "C:/VulkanSDK/1.4.357.0/Lib"
    }
    filter "system:windows" --对特定的系统(windows、OS..)、配置(Debug/Release)、平台(x64、x86)的项目属性
    cppdialect "C++17"  --C++特性版本
    staticruntime "On"  
    systemversion "10.0.22621.0"    -- windows SKD版本

    links{
        "glfw3.lib","opengl32.lib","glew32.lib","assimp-vc143-mtd.lib","comdlg32.lib","vulkan-1.lib","shaderc_shared.lib"
    }


    filter{
        "system:windows",
        "action:vs2022",
        buildoptions{"/utf-8","/MDd"}
    }
    defines
    {
        "T_PLATFORM_WINDOWS",
        "T_BUILD_DLL",
        RenderBackend == "vulkan" and "Vulkan_For_Render" or "OpenGL_For_Render"
    }


    postbuildcommands{
        ("{COPY} %{cfg.targetdir} ../bin"),
        "{COPY} ../Dependence/lib/GLEW/glew32.dll ../bin",
        "{COPY} ../Dependence/lib/assimp/assimp-vc143-mtd.dll ../bin"
    }

    