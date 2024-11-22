solution "Tsundere"

configurations {"Debug","Release"}

targetdir ("build/%{cfg.buildcfg}")


dofile("T_Core/premake5.lua")
dofile("T_Tool/premake5.lua")
dofile("T_UI/premake5.lua")
dofile("SandBox/premake5.lua")