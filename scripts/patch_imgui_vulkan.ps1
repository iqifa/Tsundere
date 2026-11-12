# Repair the pinned ImGui 1.92.3 Vulkan backend, including existing installations.
# Safe to run before every build: a patched file is validated but never rewritten.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$SourcePath)
$ErrorActionPreference = 'Stop'
$source = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath).Path)
$original = $source
$marker = '    // Backbuffers are transitioned by the render pass or dynamic rendering after acquisition.'
$functionPattern = '(?ms)^void ImGui_ImplVulkanH_CreateOrResizeWindow\([^\r\n]*\)\r?\n\{.*?^}'
$functions = [regex]::Matches($source, $functionPattern)
if ($functions.Count -ne 1) { throw 'Unsupported ImGui backend: expected one CreateOrResizeWindow definition.' }
$creation = $functions[0].Value
$lineBreak = if ($source.Contains("`r`n")) { "`r`n" } else { "`n" }
if (-not $creation.Contains($marker)) {
    $initialTransitions = '(?ms)^    // FIXME: to submit the command buffer, we need a queue\..*?(?=^})'
    if ([regex]::Matches($creation, $initialTransitions).Count -ne 1) {
        throw 'Unsupported ImGui backend: initialization transition block changed; review the patch.'
    }
    $fixedCreation = [regex]::Replace($creation, $initialTransitions, $marker + $lineBreak)
    $source = $source.Replace($creation, $fixedCreation)
    $creation = $fixedCreation
}
if ($creation.Contains('vkQueueSubmit') -or $creation.Contains('vkCmdPipelineBarrier')) {
    throw 'ImGui viewport creation must not submit or transition unacquired swapchain images.'
}
# The dynamic-rendering path clears the acquired image, so its previous contents
# can be discarded, including the first frame of a newly created swapchain.
$dynamicPattern = '(?s)(// Transition swapchain image to a layout suitable for drawing\..*?barrier.oldLayout = )VK_IMAGE_LAYOUT_(PRESENT_SRC_KHR|UNDEFINED)(;)'
if ([regex]::Matches($source, $dynamicPattern).Count -ne 1) {
    throw 'Unsupported ImGui backend: dynamic-rendering transition changed; review the patch.'
}
$source = [regex]::Replace($source, $dynamicPattern, '${1}VK_IMAGE_LAYOUT_UNDEFINED${3}')
if ($source -cne $original) {
    [IO.File]::WriteAllText((Resolve-Path -LiteralPath $SourcePath).Path, $source, [Text.UTF8Encoding]::new($false))
    Write-Host '[ImGui Vulkan] Fixed viewport swapchain transitions before acquisition.'
}
