<#
.SYNOPSIS
    Fetch / build every third-party dependency of Tsundere and put it where premake expects it.
    See DEPENDENCIES.md for the full explanation.

.EXAMPLE
    # vender sources + GLEW + GLFW, reuse an assimp you already built with CMake
    .\scripts\setup_deps.ps1 -AssimpSource F:\assimp

.EXAMPLE
    # everything from scratch: clone + build assimp, install Vulkan SDK (UAC prompt)
    .\scripts\setup_deps.ps1 -BuildAssimp -InstallVulkanSDK

.EXAMPLE
    # re-download even if files already exist
    .\scripts\setup_deps.ps1 -Force -SkipAssimp
#>
[CmdletBinding()]
param(
    [string]$ProjectRoot = (Split-Path $PSScriptRoot -Parent),
    [string]$WorkDir = (Join-Path $env:TEMP 'tsundere_deps'),
    # Existing assimp checkout that has already been built (Debug, x64, shared) with CMake.
    [string]$AssimpSource = '',
    # CMake build dir of that checkout. Defaults to <AssimpSource>\build.
    [string]$AssimpBuildDir = '',
    # Clone assimp at the pinned tag and build it with the CMake bundled in VS2022.
    [switch]$BuildAssimp,
    [switch]$SkipAssimp,
    # Download and silently install the Vulkan SDK (needs admin, shows a UAC prompt).
    [switch]$InstallVulkanSDK,
    # Overwrite libraries that are already present.
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# ---------------------------------------------------------------------------
# Pinned versions. Read DEPENDENCIES.md before bumping any of these.
# ---------------------------------------------------------------------------
$V = @{
    ImGui     = '1.92.3-docking'   # >= 1.92.4 breaks ImGUIRenderVulkan.cpp (RenderPass moved to PipelineInfoMain)
    Glm       = '1.0.3'
    Spdlog    = '1.17.0'
    StbCommit = '2c980bb59875b0d32144a71867fbdebb2f77cd20'
    EnTT      = '3.15.0'           # 3.16 removed entt::meta<T>(); 4.x needs C++20
    Glew      = '2.3.1'
    Glfw      = '3.5.1'
    Assimp    = '6.0.5'
    VulkanSDK = '1.4.357.0'        # must match the hard-coded path in the premake files
    WinSDK    = '10.0.22621.0'     # systemversion in the premake files
}

$Vender = Join-Path $ProjectRoot 'T_Core\vender'
$Dep    = Join-Path $ProjectRoot 'Dependence'
$VulkanRoot = "C:\VulkanSDK\$($V.VulkanSDK)"

function Write-Step([string]$msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Skip([string]$msg) { Write-Host "    skip: $msg" -ForegroundColor DarkGray }
function Write-Warn2([string]$msg) { Write-Host "    WARN: $msg" -ForegroundColor Yellow }

function Invoke-Native([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "'$exe $($arguments -join ' ')' failed with exit code $LASTEXITCODE" }
}

function Get-Download([string]$url, [string]$name) {
    $dest = Join-Path $WorkDir $name
    if ((Test-Path $dest) -and -not $Force) { return $dest }
    Write-Host "    download $url"
    Invoke-Native 'curl.exe' @('-fsSL', '--retry', '3', '-o', $dest, $url)
    return $dest
}

function Expand-Download([string]$url, [string]$name) {
    $zip = Get-Download $url "$name.zip"
    $out = Join-Path $WorkDir "x_$name"
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    Expand-Archive $zip $out
    # Every archive used here has exactly one top-level folder.
    return (Get-ChildItem $out -Directory | Select-Object -First 1).FullName
}

# Empties a target folder but keeps the .gitkeep placeholders tracked by git.
function Reset-Dir([string]$dir) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    Get-ChildItem $dir -Force | Where-Object { $_.Name -ne '.gitkeep' } | Remove-Item -Recurse -Force
}

function Test-Present([string]$marker, [string]$label) {
    if ((Test-Path $marker) -and -not $Force) { Write-Skip "$label already present (use -Force to refresh)"; return $true }
    return $false
}

function Find-VSInstall {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    return $path
}

New-Item -ItemType Directory -Force $WorkDir | Out-Null
Write-Host "Project root : $ProjectRoot"
Write-Host "Work dir     : $WorkDir"

# ---------------------------------------------------------------------------
# T_Core/vender : source libraries (compiled into T_Core.dll by premake)
# ---------------------------------------------------------------------------
Write-Step "ImGui $($V.ImGui)"
if (-not (Test-Present "$Vender\imgui\imgui.h" 'imgui')) {
    $src = Expand-Download "https://github.com/ocornut/imgui/archive/refs/tags/v$($V.ImGui).zip" 'imgui'
    $dst = "$Vender\imgui"; Reset-Dir $dst
    Copy-Item "$src\*.h", "$src\*.cpp", "$src\LICENSE.txt" $dst
    # Backends are flattened next to the core files; only the ones the engine uses.
    Copy-Item "$src\backends\imgui_impl_glfw.*", "$src\backends\imgui_impl_opengl3.*",
              "$src\backends\imgui_impl_opengl3_loader.h", "$src\backends\imgui_impl_vulkan.*" $dst
}

Write-Step "glm $($V.Glm)"
if (-not (Test-Present "$Vender\glm\glm.hpp" 'glm')) {
    $src = Expand-Download "https://github.com/g-truc/glm/archive/refs/tags/$($V.Glm).zip" 'glm'
    $dst = "$Vender\glm"; Reset-Dir $dst
    Copy-Item "$src\glm\*" $dst -Recurse
    Copy-Item "$src\copying.txt" $dst
    # C++20 module interface; premake would try to compile it under C++17.
    Remove-Item "$dst\glm.cppm" -ErrorAction SilentlyContinue
}

Write-Step "spdlog $($V.Spdlog)"
if (-not (Test-Present "$Vender\spdlog\spdlog.h" 'spdlog')) {
    $src = Expand-Download "https://github.com/gabime/spdlog/archive/refs/tags/v$($V.Spdlog).zip" 'spdlog'
    $dst = "$Vender\spdlog"; Reset-Dir $dst
    Copy-Item "$src\include\spdlog\*" $dst -Recurse
    Copy-Item "$src\LICENSE" $dst
}

Write-Step "stb_image ($($V.StbCommit.Substring(0,7)))"
if (-not (Test-Present "$Vender\stb_image\stb_image.h" 'stb_image')) {
    $dst = "$Vender\stb_image"; Reset-Dir $dst
    $h = Get-Download "https://raw.githubusercontent.com/nothings/stb/$($V.StbCommit)/stb_image.h" 'stb_image.h'
    Copy-Item $h $dst
    # Nothing else in the engine defines STB_IMAGE_IMPLEMENTATION.
    Set-Content -Encoding ASCII "$dst\stb_image.cpp" "#define STB_IMAGE_IMPLEMENTATION`r`n#include `"stb_image.h`""
}

Write-Step "EnTT $($V.EnTT)"
if (-not (Test-Present "$Vender\entt\entt.hpp" 'entt')) {
    $dst = "$Vender\entt"; Reset-Dir $dst
    $h = Get-Download "https://raw.githubusercontent.com/skypjack/entt/v$($V.EnTT)/single_include/entt/entt.hpp" 'entt.hpp'
    $l = Get-Download "https://raw.githubusercontent.com/skypjack/entt/v$($V.EnTT)/LICENSE" 'entt_LICENSE'
    Copy-Item $h $dst
    Copy-Item $l "$dst\LICENSE"
}

# ---------------------------------------------------------------------------
# Dependence : prebuilt x64 binaries
# ---------------------------------------------------------------------------
Write-Step "GLEW $($V.Glew) (win32 package, x64 files)"
if (-not (Test-Present "$Dep\lib\GLEW\glew32.lib" 'GLEW')) {
    $src = Expand-Download "https://github.com/nigels-com/glew/releases/download/glew-$($V.Glew)/glew-$($V.Glew)-win32.zip" 'glew'
    Reset-Dir "$Dep\include\GL"; Reset-Dir "$Dep\lib\GLEW"
    Copy-Item "$src\include\GL\*" "$Dep\include\GL"
    Copy-Item "$src\lib\Release\x64\*", "$src\bin\Release\x64\glew32.dll" "$Dep\lib\GLEW"
}

Write-Step "GLFW $($V.Glfw) (WIN64, lib-vc2022)"
if (-not (Test-Present "$Dep\lib\GLFW\glfw3.lib" 'GLFW')) {
    $src = Expand-Download "https://github.com/glfw/glfw/releases/download/$($V.Glfw)/glfw-$($V.Glfw).bin.WIN64.zip" 'glfw'
    Reset-Dir "$Dep\include\GLFW"; Reset-Dir "$Dep\lib\GLFW"
    Copy-Item "$src\include\GLFW\*" "$Dep\include\GLFW"
    Copy-Item "$src\lib-vc2022\*" "$Dep\lib\GLFW"
}

# ---------------------------------------------------------------------------
# assimp : must be built from source with CMake
# ---------------------------------------------------------------------------
Write-Step "assimp"
$assimpLibName = 'assimp-vc143-mtd'
if ($SkipAssimp) {
    Write-Skip 'assimp (-SkipAssimp)'
} elseif ((Test-Path "$Dep\lib\assimp\$assimpLibName.lib") -and -not $Force -and -not $AssimpSource -and -not $BuildAssimp) {
    Write-Skip 'assimp already present (pass -AssimpSource or -BuildAssimp to replace)'
} else {
    if ($BuildAssimp) {
        $vs = Find-VSInstall
        if (-not $vs) { throw 'Visual Studio 2022 with the C++ workload was not found (needed to build assimp).' }
        $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (-not (Test-Path $cmake)) { $cmake = 'cmake' }

        $AssimpSource = Join-Path $WorkDir "assimp-$($V.Assimp)"
        $AssimpBuildDir = Join-Path $AssimpSource 'build'
        if (-not (Test-Path "$AssimpSource\CMakeLists.txt")) {
            Invoke-Native 'git' @('clone', '--quiet', '--depth', '1', '--branch', "v$($V.Assimp)", 'https://github.com/assimp/assimp.git', $AssimpSource)
        }
        Write-Host '    configuring (VS2022 generator, x64, shared, bundled zlib)'
        Invoke-Native $cmake @('-S', $AssimpSource, '-B', $AssimpBuildDir,
            '-G', 'Visual Studio 17 2022', '-A', 'x64',
            '-DBUILD_SHARED_LIBS=ON',
            '-DUSE_STATIC_CRT=OFF',
            '-DASSIMP_BUILD_ZLIB=ON',
            '-DASSIMP_BUILD_TESTS=OFF',
            '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF',
            '-DASSIMP_BUILD_SAMPLES=OFF',
            '-DASSIMP_INSTALL=OFF',
            '-DASSIMP_WARNINGS_AS_ERRORS=OFF')
        Write-Host '    building Debug (takes several minutes)'
        Invoke-Native $cmake @('--build', $AssimpBuildDir, '--config', 'Debug', '--parallel')
    }

    if (-not $AssimpSource) {
        Write-Warn2 'assimp not installed. Pass -AssimpSource <dir> (already built) or -BuildAssimp.'
    } else {
        if (-not $AssimpBuildDir) { $AssimpBuildDir = Join-Path $AssimpSource 'build' }
        $lib = "$AssimpBuildDir\lib\Debug\$assimpLibName.lib"
        $dll = "$AssimpBuildDir\bin\Debug\$assimpLibName.dll"
        if (-not (Test-Path $lib) -or -not (Test-Path $dll)) {
            throw "Missing $lib / $dll. Build assimp Debug|x64 with the VS2022 (v143) toolset and BUILD_SHARED_LIBS=ON first."
        }
        Reset-Dir "$Dep\include\assimp"; Reset-Dir "$Dep\lib\assimp"
        # Public headers live in the source tree, config.h / revision.h are generated into the build tree.
        Copy-Item "$AssimpSource\include\assimp\*" "$Dep\include\assimp" -Recurse -Exclude '*.in'
        Get-ChildItem -Recurse "$Dep\include\assimp" -Filter '*.in' | Remove-Item
        Copy-Item "$AssimpBuildDir\include\assimp\config.h", "$AssimpBuildDir\include\assimp\revision.h" "$Dep\include\assimp" -Force
        Copy-Item $lib, $dll "$Dep\lib\assimp"
        $pdb = "$AssimpBuildDir\bin\Debug\$assimpLibName.pdb"
        if (Test-Path $pdb) { Copy-Item $pdb "$Dep\lib\assimp" }
        Write-Host "    installed from $AssimpSource"
    }
}

# ---------------------------------------------------------------------------
# Vulkan SDK : needed for BOTH backends (premake always links vulkan-1 / shaderc_shared)
# ---------------------------------------------------------------------------
Write-Step "Vulkan SDK $($V.VulkanSDK)"
if (Test-Path "$VulkanRoot\Lib\vulkan-1.lib") {
    Write-Skip "found at $VulkanRoot"
} elseif ($InstallVulkanSDK) {
    $exe = Get-Download "https://sdk.lunarg.com/sdk/download/$($V.VulkanSDK)/windows/vulkansdk-windows-X64-$($V.VulkanSDK).exe" 'vulkansdk.exe'
    $sig = Get-AuthenticodeSignature $exe
    if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'LunarG') { throw "Vulkan SDK installer signature check failed: $($sig.Status)" }
    Write-Host '    installing silently (accept the UAC prompt)'
    $p = Start-Process -FilePath $exe -Verb RunAs -Wait -PassThru -ArgumentList @(
        '--root', $VulkanRoot, '--accept-licenses', '--default-answer', '--confirm-command', 'install')
    if ($p.ExitCode -ne 0) { throw "Vulkan SDK installer exited with $($p.ExitCode)" }
} else {
    Write-Warn2 "not installed. Re-run with -InstallVulkanSDK, or install it from https://vulkan.lunarg.com/sdk/home to $VulkanRoot"
}

# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------
Write-Step 'Verify'
$required = [ordered]@{
    'vender imgui'            = "$Vender\imgui\imgui_impl_vulkan.cpp"
    'vender glm'              = "$Vender\glm\glm.hpp"
    'vender spdlog'           = "$Vender\spdlog\spdlog.h"
    'vender stb_image'        = "$Vender\stb_image\stb_image.cpp"
    'vender entt'             = "$Vender\entt\entt.hpp"
    'GLEW header'             = "$Dep\include\GL\glew.h"
    'GLEW lib/dll'            = "$Dep\lib\GLEW\glew32.dll"
    'GLFW header'             = "$Dep\include\GLFW\glfw3.h"
    'GLFW lib'                = "$Dep\lib\GLFW\glfw3.lib"
    'assimp header'           = "$Dep\include\assimp\config.h"
    'assimp lib'              = "$Dep\lib\assimp\$assimpLibName.lib"
    'assimp dll'              = "$Dep\lib\assimp\$assimpLibName.dll"
    'Vulkan SDK vulkan-1.lib' = "$VulkanRoot\Lib\vulkan-1.lib"
    'Vulkan SDK shaderc'      = "$VulkanRoot\Lib\shaderc_shared.lib"
    "Windows SDK $($V.WinSDK)" = "${env:ProgramFiles(x86)}\Windows Kits\10\Include\$($V.WinSDK)"
}
$missing = 0
foreach ($k in $required.Keys) {
    if (Test-Path $required[$k]) { Write-Host ("    [ OK ] {0}" -f $k) -ForegroundColor Green }
    else { Write-Host ("    [MISS] {0}  ({1})" -f $k, $required[$k]) -ForegroundColor Red; $missing++ }
}
if ($missing -gt 0) {
    Write-Host "`n$missing item(s) missing, see DEPENDENCIES.md." -ForegroundColor Red
    exit 1
}
Write-Host "`nAll dependencies in place. Next: .\premake5.exe vs2022   (or --renderer=vulkan)" -ForegroundColor Green
