@echo off
rem Usage: setup_deps.bat [-AssimpSource F:\assimp] [-BuildAssimp] [-InstallVulkanSDK] [-Force] [-SkipAssimp] [-CleanCache]
rem Pass -ProjectRoot explicitly: $PSScriptRoot can be empty when the .ps1 is
rem launched via -File from some hosts (e.g. a shell without script context).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup_deps.ps1" -ProjectRoot "%~dp0." %*
