#include "Platform/RHI/RHIRayTracing.h"
#include "Platform/RenderAPIConfig.h"

// OpenGL 没有硬件光追接口：PathTracePass 在 GL 上始终使用软件 BVH。
#ifdef RenderAPI_OpenGL
bool RHIRayTracingScene::IsSupported()
{
	return false;
}

std::string RHIRayTracingScene::GetUnsupportedReason()
{
	return "OpenGL backend has no hardware ray tracing; using the software BVH";
}

Ref<RHIRayTracingScene> RHIRayTracingScene::Create()
{
	return nullptr;
}
#endif
