#include<Platform/RHI/RHICommandBuffer.h>
#include<Platform/RenderAPI.h>

#ifdef RenderAPI_Vulkan
Ref<RHICommandBuffer> RHICommandBuffer::Create()
{
    return nullptr;
}
#endif // RenderAPI_Vulkan