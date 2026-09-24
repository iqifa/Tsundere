@echo off
rem Usage: setup_deps.bat [-AssimpSource F:\assimp] [-BuildAssimp] [-InstallVulkanSDK] [-Force] [-SkipAssimp]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup_deps.ps1" %*
