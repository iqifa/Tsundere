#define GLFW_INCLUDE_VULKAN
#include "VulkanContext.h"
#include "Platform/RenderAPI.h"
#include "Debug/Debug.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace
{
#ifdef _DEBUG
    constexpr bool kEnableValidation = true;
#else
    constexpr bool kEnableValidation = false;
#endif

    constexpr const char* kValidationLayer =
        "VK_LAYER_KHRONOS_validation";

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void*)
    {
        const char* message = callbackData && callbackData->pMessage
            ? callbackData->pMessage
            : "Unknown Vulkan validation message";

        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            Error_Core("[Vulkan Validation] {}", message);
        }
        else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            Warn_Core("[Vulkan Validation] {}", message);
        }
        else
        {
            Info_Core("[Vulkan Validation] {}", message);
        }

        (void)type;
        return VK_FALSE;
    }

    void PopulateDebugMessengerCreateInfo(
        VkDebugUtilsMessengerCreateInfoEXT& createInfo)
    {
        createInfo = {};
        createInfo.sType =
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = VulkanDebugCallback;
    }

    bool HasValidationLayer()
    {
        uint32_t count = 0;
        if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
            return false;

        std::vector<VkLayerProperties> layers(count);
        if (count > 0 &&
            vkEnumerateInstanceLayerProperties(&count, layers.data()) !=
                VK_SUCCESS)
        {
            return false;
        }

        return std::any_of(
            layers.begin(),
            layers.end(),
            [](const VkLayerProperties& layer)
            {
                return std::strcmp(layer.layerName, kValidationLayer) == 0;
            });
    }

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
    m_RHICommandBuffer = RHICommandBuffer::Create();

    m_Initialized = true;
    Info_Core("VulkanContext initialized");
}

void VulkanContext::Shutdown()
{
    if (!m_Initialized && m_Instance == VK_NULL_HANDLE)
        return;

    WaitIdle();
    m_FrameActive = false;
    m_PresentPassActive = false;
    m_PresentPassRecorded = false;
    m_RHICommandBuffer.reset();
    m_SwapChain.reset();

    if (m_Device != VK_NULL_HANDLE)
    {
        if (m_InFlightFence != VK_NULL_HANDLE)
            vkDestroyFence(m_Device, m_InFlightFence, nullptr);
        for (VkSemaphore semaphore : m_RenderFinishedSemaphores)
        {
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Device, semaphore, nullptr);
        }
        m_RenderFinishedSemaphores.clear();
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
    if (m_DebugMessenger != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE)
    {
        auto destroyDebugMessenger =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(
                    m_Instance,
                    "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyDebugMessenger)
        {
            destroyDebugMessenger(
                m_Instance,
                m_DebugMessenger,
                nullptr);
        }
    }
    if (m_Instance != VK_NULL_HANDLE)
        vkDestroyInstance(m_Instance, nullptr);

    m_InFlightFence = VK_NULL_HANDLE;
    m_CurrentRenderFinishedSemaphore = VK_NULL_HANDLE;
    m_RenderFinishedSemaphores.clear();
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
    m_DebugMessenger = VK_NULL_HANDLE;
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

    // The swapchain render pass remains the presentation scope used by ImGui.
    // RDG offscreen passes use dynamic rendering and are recorded separately.
    ++m_FrameContext.FrameIndex;
    m_FrameContext.ImageIndex = imageIndex;
    m_CurrentRenderFinishedSemaphore =
        imageIndex < m_RenderFinishedSemaphores.size()
            ? m_RenderFinishedSemaphores[imageIndex]
            : VK_NULL_HANDLE;
    m_PresentPassActive = false;
    m_PresentPassRecorded = false;
    m_FrameActive = true;

    if (acquireResult == VK_SUBOPTIMAL_KHR)
        m_FramebufferResized = true;
}

void VulkanContext::BeginPresentPass()
{
    if (!m_Initialized || !m_FrameActive || m_PresentPassActive ||
        m_PresentPassRecorded ||
        m_FrameContext.ImageIndex >= m_Framebuffers.size())
    {
        return;
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_Framebuffers[m_FrameContext.ImageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_Extent;

    VkClearValue clearColor = { {{ 0.1f, 0.2f, 0.4f, 1.0f }} };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(
        m_CommandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE);
    m_PresentPassActive = true;
}

void VulkanContext::EndPresentPass()
{
    if (!m_PresentPassActive)
        return;

    vkCmdEndRenderPass(m_CommandBuffer);
    m_PresentPassActive = false;
    m_PresentPassRecorded = true;
}

void VulkanContext::EndFrame()
{
    if (!m_Initialized || !m_FrameActive)
        return;

    if (m_PresentPassActive)
        EndPresentPass();

    // Even when the UI path skipped drawing, execute the presentation pass once
    // so its final layout transition moves the acquired image to PRESENT_SRC_KHR.
    if (!m_PresentPassRecorded)
    {
        BeginPresentPass();
        EndPresentPass();
    }

    CheckVk(vkEndCommandBuffer(m_CommandBuffer),
            "Failed to end Vulkan command buffer");

    VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };
    if (m_CurrentRenderFinishedSemaphore == VK_NULL_HANDLE)
    {
        Error_Core("VulkanContext: acquired image has no render-finished semaphore");
        return;
    }

    VkSemaphore signalSemaphores[] = { m_CurrentRenderFinishedSemaphore };

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
    m_PresentPassActive = false;
    m_PresentPassRecorded = false;

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
    // VulkanContext owns native begin/end/submit; the RHI wrapper records commands
    // into the context's currently active VkCommandBuffer.
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
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions || glfwExtensionCount == 0)
        throw std::runtime_error("GLFW did not provide Vulkan instance extensions");

    std::vector<const char*> extensions(
        glfwExtensions,
        glfwExtensions + glfwExtensionCount);

    const bool enableValidation =
        kEnableValidation && HasValidationLayer();
    if (kEnableValidation && !enableValidation)
    {
        Warn_Core(
            "Vulkan validation requested, but {} is unavailable",
            kValidationLayer);
    }

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidation)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        PopulateDebugMessengerCreateInfo(debugCreateInfo);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = enableValidation ? 1u : 0u;
    createInfo.ppEnabledLayerNames =
        enableValidation ? &kValidationLayer : nullptr;
    createInfo.pNext = enableValidation ? &debugCreateInfo : nullptr;

    CheckVk(vkCreateInstance(&createInfo, nullptr, &m_Instance),
            "Failed to create Vulkan instance");

    if (!enableValidation)
        return;

    auto createDebugMessenger =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(
                m_Instance,
                "vkCreateDebugUtilsMessengerEXT"));
    if (!createDebugMessenger)
    {
        Warn_Core("Vulkan debug utils extension entry point is unavailable");
        return;
    }

    CheckVk(
        createDebugMessenger(
            m_Instance,
            &debugCreateInfo,
            nullptr,
            &m_DebugMessenger),
        "Failed to create Vulkan debug messenger");
    Info_Core("Vulkan validation enabled");
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

    VkPhysicalDeviceVulkan13Features supportedFeatures13{};
    supportedFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 supportedFeatures{};
    supportedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supportedFeatures.pNext = &supportedFeatures13;
    vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &supportedFeatures);
    if (!supportedFeatures13.dynamicRendering)
        throw std::runtime_error("Selected Vulkan device does not support dynamic rendering");

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    CheckVk(vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device),
            "Failed to create Vulkan logical device");
    m_DynamicRenderingSupported = true;
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

    m_RenderFinishedSemaphores.resize(m_SwapchainImages.size(), VK_NULL_HANDLE);
    for (VkSemaphore& semaphore : m_RenderFinishedSemaphores)
    {
        CheckVk(vkCreateSemaphore(
                    m_Device, &semaphoreInfo, nullptr, &semaphore),
                "Failed to create Vulkan render-finished semaphore");
    }

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

    for (VkSemaphore semaphore : m_RenderFinishedSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(m_Device, semaphore, nullptr);
    }
    m_RenderFinishedSemaphores.clear();
    m_RenderFinishedSemaphores.resize(m_SwapchainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& semaphore : m_RenderFinishedSemaphores)
    {
        CheckVk(vkCreateSemaphore(
                    m_Device, &semaphoreInfo, nullptr, &semaphore),
                "Failed to recreate Vulkan render-finished semaphore");
    }
    m_CurrentRenderFinishedSemaphore = VK_NULL_HANDLE;

    createRenderPass();
    createFramebuffers();
    ++m_SwapchainGeneration;
    m_FramebufferResized = false;
}
