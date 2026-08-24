#define GLFW_INCLUDE_VULKAN
#include "VulkanContext.h"
#include "Platform/RenderAPI.h"
#include "Debug/Debug.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace
{
    void CheckVk(VkResult result, const char* operation)
    {
        if (result != VK_SUCCESS)
            throw std::runtime_error(operation);
    }
}

#ifdef RenderAPI_Vulkan
static Ref<RHIContext> s_RHIContext;

Ref<RHIContext>& RHIContext::Get()
{
    return s_RHIContext;
}

Ref<RHIContext> RHIContext::Create(GLFWwindow* window)
{
    auto context = CreateRef<VulkanContext>(window);
    context->Init(window);
    s_RHIContext = context;
    return context;
}
#endif

VulkanContext::VulkanContext(GLFWwindow* window)
    : m_Window(window)
{
}

VulkanContext::~VulkanContext()
{
    Shutdown();
}

void VulkanContext::Init(GLFWwindow* window)
{
    if (m_Initialized)
        return;

    m_Window = window;

    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createRenderPass();
    createFramebuffers();
    createDescriptorPool();
    createCommandAndSyncObjects();

    m_Initialized = true;
    Info_Core("VulkanContext initialized");
}

void VulkanContext::Shutdown()
{
    if (!m_Initialized && m_Instance == VK_NULL_HANDLE)
        return;

    WaitIdle();
    m_FrameActive = false;
    m_RHICommandBuffer.reset();
    m_SwapChain.reset();

    if (m_Device != VK_NULL_HANDLE)
    {
        if (m_InFlightFence != VK_NULL_HANDLE)
            vkDestroyFence(m_Device, m_InFlightFence, nullptr);
        if (m_RenderFinishedSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(m_Device, m_RenderFinishedSemaphore, nullptr);
        if (m_ImageAvailableSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(m_Device, m_ImageAvailableSemaphore, nullptr);
        if (m_CommandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        if (m_DescriptorPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);

        cleanupSwapchain();
        vkDestroyDevice(m_Device, nullptr);
    }

    if (m_Surface != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    if (m_Instance != VK_NULL_HANDLE)
        vkDestroyInstance(m_Instance, nullptr);

    m_InFlightFence = VK_NULL_HANDLE;
    m_RenderFinishedSemaphore = VK_NULL_HANDLE;
    m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    m_CommandBuffer = VK_NULL_HANDLE;
    m_CommandPool = VK_NULL_HANDLE;
    m_DescriptorPool = VK_NULL_HANDLE;
    m_RenderPass = VK_NULL_HANDLE;
    m_Swapchain = VK_NULL_HANDLE;
    m_Queue = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
    m_PhysicalDevice = VK_NULL_HANDLE;
    m_Surface = VK_NULL_HANDLE;
    m_Instance = VK_NULL_HANDLE;
    m_Initialized = false;

    Info_Core("VulkanContext shutdown");
}

void VulkanContext::BeginFrame()
{
    if (!m_Initialized || m_FrameActive)
        return;

    if (m_FramebufferResized)
    {
        recreateSwapchain();
        if (m_FramebufferResized)
            return;
    }

    CheckVk(vkWaitForFences(m_Device, 1, &m_InFlightFence, VK_TRUE,
                            std::numeric_limits<uint64_t>::max()),
            "Failed to wait for Vulkan frame fence");

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        m_Device, m_Swapchain, std::numeric_limits<uint64_t>::max(),
        m_ImageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        CheckVk(acquireResult, "Failed to acquire Vulkan swapchain image");

    CheckVk(vkResetFences(m_Device, 1, &m_InFlightFence),
            "Failed to reset Vulkan frame fence");
    CheckVk(vkResetCommandBuffer(m_CommandBuffer, 0),
            "Failed to reset Vulkan command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(m_CommandBuffer, &beginInfo),
            "Failed to begin Vulkan command buffer");

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_Framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_Extent;

    VkClearValue clearColor = { {{ 0.1f, 0.2f, 0.4f, 1.0f }} };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(m_CommandBuffer, &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    ++m_FrameContext.FrameIndex;
    m_FrameContext.ImageIndex = imageIndex;
    m_FrameActive = true;

    if (acquireResult == VK_SUBOPTIMAL_KHR)
        m_FramebufferResized = true;
}

void VulkanContext::EndFrame()
{
    if (!m_Initialized || !m_FrameActive)
        return;

    vkCmdEndRenderPass(m_CommandBuffer);
    CheckVk(vkEndCommandBuffer(m_CommandBuffer),
            "Failed to end Vulkan command buffer");

    VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };
    VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphore };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_CommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    CheckVk(vkQueueSubmit(m_Queue, 1, &submitInfo, m_InFlightFence),
            "Failed to submit Vulkan frame");

    VkSwapchainKHR swapchains[] = { m_Swapchain };
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &m_FrameContext.ImageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_Queue, &presentInfo);
    m_FrameActive = false;

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR || m_FramebufferResized)
    {
        recreateSwapchain();
        return;
    }

    CheckVk(presentResult, "Failed to present Vulkan frame");

    // GLFW dispatches ImGui's mouse/keyboard callbacks from its event queue.
    // The Vulkan path does not swap through Window::OnUpdate(), so poll here
    // once per completed frame just like GLContext does.
    glfwPollEvents();
}

void VulkanContext::OnResize(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0)
        return;

    m_FramebufferResized = true;
}

void VulkanContext::WaitIdle()
{
    if (m_Device != VK_NULL_HANDLE)
        CheckVk(vkDeviceWaitIdle(m_Device), "Failed to wait for Vulkan device");
}

Ref<RHISwapChain> VulkanContext::GetSwapChain()
{
    return m_SwapChain;
}

Ref<RHICommandBuffer> VulkanContext::GetCommandBuffer()
{
    // The native command buffer is available through RHIVulkanContext. A Vulkan
    // RHICommandBuffer wrapper has not been implemented yet, so do not expose a
    // misleading non-null object here.
    return m_RHICommandBuffer;
}

const RHIFrameContext& VulkanContext::GetCurrentFrame() const
{
    return m_FrameContext;
}

void VulkanContext::createInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Tsundere";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Tsundere";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (!extensions || extensionCount == 0)
        throw std::runtime_error("GLFW did not provide Vulkan instance extensions");

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;

    CheckVk(vkCreateInstance(&createInfo, nullptr, &m_Instance),
            "Failed to create Vulkan instance");
}

void VulkanContext::createSurface()
{
    CheckVk(glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface),
            "Failed to create Vulkan window surface");
}

void VulkanContext::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    CheckVk(vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr),
            "Failed to enumerate Vulkan physical devices");
    if (deviceCount == 0)
        throw std::runtime_error("No Vulkan physical device is available");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    CheckVk(vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data()),
            "Failed to enumerate Vulkan physical devices");

    for (VkPhysicalDevice device : devices)
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(
            device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t family = 0; family < queueFamilyCount; ++family)
        {
            VkBool32 supportsPresent = VK_FALSE;
            CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(
                        device, family, m_Surface, &supportsPresent),
                    "Failed to query Vulkan presentation support");

            if ((queueFamilies[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                supportsPresent)
            {
                m_PhysicalDevice = device;
                m_QueueFamily = family;
                return;
            }
        }
    }

    throw std::runtime_error(
        "No Vulkan queue family supports both graphics and presentation");
}

void VulkanContext::createLogicalDevice()
{
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_QueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    CheckVk(vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device),
            "Failed to create Vulkan logical device");
    vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
}

void VulkanContext::createSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                m_PhysicalDevice, m_Surface, &capabilities),
            "Failed to query Vulkan surface capabilities");

    uint32_t formatCount = 0;
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                m_PhysicalDevice, m_Surface, &formatCount, nullptr),
            "Failed to query Vulkan surface formats");
    if (formatCount == 0)
        throw std::runtime_error("Vulkan surface has no supported formats");

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                m_PhysicalDevice, m_Surface, &formatCount, formats.data()),
            "Failed to query Vulkan surface formats");

    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surfaceFormat = format;
            break;
        }
    }

    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        m_Extent = capabilities.currentExtent;
    }
    else
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_Window, &width, &height);
        m_Extent.width = std::clamp(static_cast<uint32_t>(width),
                                    capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width);
        m_Extent.height = std::clamp(static_cast<uint32_t>(height),
                                     capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height);
    }

    m_MinImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0)
        m_MinImageCount = std::min(m_MinImageCount, capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_Surface;
    createInfo.minImageCount = m_MinImageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = m_Extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    CheckVk(vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_Swapchain),
            "Failed to create Vulkan swapchain");

    uint32_t imageCount = 0;
    CheckVk(vkGetSwapchainImagesKHR(
                m_Device, m_Swapchain, &imageCount, nullptr),
            "Failed to query Vulkan swapchain images");
    m_SwapchainImages.resize(imageCount);
    CheckVk(vkGetSwapchainImagesKHR(
                m_Device, m_Swapchain, &imageCount, m_SwapchainImages.data()),
            "Failed to query Vulkan swapchain images");

    m_SwapchainImageViews.resize(imageCount);
    for (size_t i = 0; i < m_SwapchainImages.size(); ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_SwapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        CheckVk(vkCreateImageView(
                    m_Device, &viewInfo, nullptr, &m_SwapchainImageViews[i]),
                "Failed to create Vulkan swapchain image view");
    }
}

void VulkanContext::createRenderPass()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                m_PhysicalDevice, m_Surface, &capabilities),
            "Failed to query Vulkan surface capabilities");

    uint32_t formatCount = 0;
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                m_PhysicalDevice, m_Surface, &formatCount, nullptr),
            "Failed to query Vulkan surface formats");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                m_PhysicalDevice, m_Surface, &formatCount, formats.data()),
            "Failed to query Vulkan surface formats");

    VkFormat colorFormat = formats[0].format;
    for (const auto& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            colorFormat = format.format;
            break;
        }
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    CheckVk(vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass),
            "Failed to create Vulkan render pass");
}

void VulkanContext::createFramebuffers()
{
    m_Framebuffers.resize(m_SwapchainImageViews.size());
    for (size_t i = 0; i < m_SwapchainImageViews.size(); ++i)
    {
        VkImageView attachments[] = { m_SwapchainImageViews[i] };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_Extent.width;
        framebufferInfo.height = m_Extent.height;
        framebufferInfo.layers = 1;

        CheckVk(vkCreateFramebuffer(
                    m_Device, &framebufferInfo, nullptr, &m_Framebuffers[i]),
                "Failed to create Vulkan framebuffer");
    }
}

void VulkanContext::createDescriptorPool()
{
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    CheckVk(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool),
            "Failed to create Vulkan descriptor pool");
}

void VulkanContext::createCommandAndSyncObjects()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_QueueFamily;
    CheckVk(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool),
            "Failed to create Vulkan command pool");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    CheckVk(vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer),
            "Failed to allocate Vulkan command buffer");

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    CheckVk(vkCreateSemaphore(
                m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphore),
            "Failed to create Vulkan image-available semaphore");
    CheckVk(vkCreateSemaphore(
                m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphore),
            "Failed to create Vulkan render-finished semaphore");
    CheckVk(vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFence),
            "Failed to create Vulkan frame fence");
}

void VulkanContext::cleanupSwapchain()
{
    if (m_Device == VK_NULL_HANDLE)
        return;

    for (VkFramebuffer framebuffer : m_Framebuffers)
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    m_Framebuffers.clear();

    if (m_RenderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
    m_RenderPass = VK_NULL_HANDLE;

    for (VkImageView imageView : m_SwapchainImageViews)
        vkDestroyImageView(m_Device, imageView, nullptr);
    m_SwapchainImageViews.clear();
    m_SwapchainImages.clear();

    if (m_Swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
    m_Swapchain = VK_NULL_HANDLE;
}

void VulkanContext::recreateSwapchain()
{
    if (!m_Initialized)
        return;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height);
    if (width == 0 || height == 0)
    {
        m_FramebufferResized = true;
        return;
    }

    WaitIdle();
    cleanupSwapchain();
    createSwapchain();
    createRenderPass();
    createFramebuffers();
    ++m_SwapchainGeneration;
    m_FramebufferResized = false;
}
