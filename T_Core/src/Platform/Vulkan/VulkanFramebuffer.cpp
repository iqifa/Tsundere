#include "VulkanFramebuffer.h"
#include "Platform/RenderAPIConfig.h"

#ifdef RenderAPI_Vulkan
Ref<RHIFramebuffer> RHIFramebuffer::Create(const FramebufferDesc& desc)
{
    return CreateRef<VulkanFramebuffer>(desc);
}

Ref<RHIFramebuffer> RHIFramebuffer::CreateView(const FramebufferViewDesc& desc)
{
    return CreateRef<VulkanFramebufferView>(desc);
}
#endif
