@echo off
rem Commit and push the GPU path tracing work to origin/Sandbox. Run from anywhere.
setlocal
cd /d "%~dp0"
for /f %%b in ('git rev-parse --abbrev-ref HEAD') do set BRANCH=%%b
if /i not "%BRANCH%"=="Sandbox" ( echo Current branch is %BRANCH%, expected Sandbox. & exit /b 1 )
git add -- SandBox/src/Editor_Panel/VulkanExample.cpp SandBox/src/Editor_Panel/VulkanExample.h T_Core/src/Pipeline/Passes/PathTracingPass.h T_Core/src/Pipeline/PathTracing/PathTraceScene.cpp T_Core/src/Pipeline/PathTracing/PathTraceScene.h T_Core/src/Pipeline/RenderGraph.cpp T_Core/src/Platform/GL/GLDescriptorSet.cpp T_Core/src/Platform/GL/GLDescriptorSet.h T_Core/src/Platform/GL/GLGpuTimer.cpp T_Core/src/Platform/GL/GLRayTracing.cpp T_Core/src/Platform/RHI/RHICommandBuffer.h T_Core/src/Platform/RHI/RHIDescriptorSet.h T_Core/src/Platform/RHI/RHIGpuTimer.h T_Core/src/Platform/RHI/RHIRayTracing.h T_Core/src/Platform/RHI/RHIShaderReflection.h T_Core/src/Platform/RHI/RHIVulkanContext.h T_Core/src/Platform/ShaderCompiler.cpp T_Core/src/Platform/Vulkan/VulkanCommandBuffer.cpp T_Core/src/Platform/Vulkan/VulkanCommandBuffer.h T_Core/src/Platform/Vulkan/VulkanContext.cpp T_Core/src/Platform/Vulkan/VulkanContext.h T_Core/src/Platform/Vulkan/VulkanDeletionQueue.h T_Core/src/Platform/Vulkan/VulkanDescriptorSet.cpp T_Core/src/Platform/Vulkan/VulkanDescriptorSet.h T_Core/src/Platform/Vulkan/VulkanGpuTimer.cpp T_Core/src/Platform/Vulkan/VulkanPipeline.cpp T_Core/src/Platform/Vulkan/VulkanPipeline.h T_Core/src/Platform/Vulkan/VulkanRayTracing.cpp T_Core/src/Platform/Vulkan/VulkanRayTracing.h T_Core/src/Scene/BVH.cpp T_Core/src/Scene/BVH.h T_Core/src/Scene/BVHBuilder.cpp T_Core/src/Scene/BVHBuilder.h overview/GPU_PathTracing.md res/shaders/PathTraceBVH.shader res/shaders/PathTraceBVHSky.shader res/shaders/PathTraceRayQuery.shader res/shaders/PathTraceRayQuerySky.shader res/shaders/include/BVH.glsl res/shaders/include/PathTraceCommon.glsl res/shaders/include/PathTraceIntegrator.glsl res/shaders/include/PathTraceTraceBVH.glsl res/shaders/include/PathTraceTraceRayQuery.glsl tests/BVHTests.cpp 
if errorlevel 1 exit /b 1
git commit -F build\pt_commit_msg.txt
if errorlevel 1 exit /b 1
git push origin Sandbox
if errorlevel 1 exit /b 1
echo Done. You can delete commit_pt.cmd and build\pt_commit_msg.txt.
