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
        "src/Scene/SceneRender.h",
        "src/Pipeline/Passes/ShadowPass.h",
        "src/Pipeline/Passes/ShadowApplyPass.h",
        "src/Pipeline/Passes/PathTracePass.h",
        "src/Pipeline/Passes/DDGIPass.h",
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
            "src/Panels/Platform/Vulkan/**.cpp",
            -- GL backend still compiles shaders through SPIR-V (shaderc +
            -- SPIRV-Cross), but does not use the Vulkan API itself.
            "vender/imgui/imgui_impl_vulkan.h",
            "vender/imgui/imgui_impl_vulkan.cpp"
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
        VULKAN_SDK_ROOT and (VULKAN_SDK_ROOT .. "/Include") or "",
        -- "vender/spdlog/include"
    }
    libdirs{
        "../Dependence/lib/GLFW/",
        "../Dependence/lib/GLEW/",
        "../Dependence/lib/assimp/",
        VULKAN_SDK_ROOT and (VULKAN_SDK_ROOT .. "/Lib") or ""
    }
    filter "system:windows" --对特定的系统(windows、OS..)、配置(Debug/Release)、平台(x64、x86)的项目属性
    cppdialect "C++17"  --C++特性版本
    staticruntime "On"
    systemversion (WINSDK_VERSION or "10.0.22621.0")    -- windows SDK版本,由 premake5-defs.lua 探测

    links{
        "glfw3.lib","opengl32.lib","glew32.lib", ASSIMP_LIB or "assimp-vc143-mtd.lib","comdlg32.lib",
        "shaderc_shared.lib","spirv-cross-c-shared.lib"
    }
    -- GL backend does not use the Vulkan API itself (shaders still compile
    -- through shaderc/SPIRV-Cross from the SDK), so skip vulkan-1.
    if RenderBackend == "vulkan" then
        links{ "vulkan-1.lib" }
    end


    filter{ "system:windows", "action:vs*" }
        buildoptions{"/MDd","/FS"}
    filter{}
    defines
    {
        "T_PLATFORM_WINDOWS",
        "T_BUILD_DLL",
        "IMGUI_API=__declspec(dllexport)",
        RenderBackend == "vulkan" and "Vulkan_For_Render" or "OpenGL_For_Render"
    }


    postbuildcommands{
        '{COPYDIR} "%{cfg.targetdir}" "../bin"',
        '{COPYFILE} "../Dependence/lib/GLEW/glew32.dll" "../bin"',
        '{COPYFILE} "../Dependence/lib/assimp/' .. (ASSIMP_LIB or "assimp-vc143-mtd") .. '.dll" "../bin"',
        '{COPYFILE} "' .. (VULKAN_SDK_ROOT or "C:/VulkanSDK/1.4.357.0") .. '/Bin/shaderc_shared.dll" "../bin"',
        '{COPYFILE} "' .. (VULKAN_SDK_ROOT or "C:/VulkanSDK/1.4.357.0") .. '/Bin/spirv-cross-c-shared.dll" "../bin"'
    }
