#pragma once

#define GLFW_INCLUDE_VULKAN
#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include <vulkan/vulkan.h>
#include <vector>

class T_API VulkanContext : public RHIContext, public RHIVulkanContext
{
public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext() override;

    void Init(GLFWwindow* window) override;
    void EndFrame() override;
    void OnResize(uint32_t w, uint32_t h) override;
    void Shutdown() override;
    void BeginFrame() override;
    void WaitIdle() override;

    Ref<RHISwapChain> GetSwapChain() override;
    Ref<RHICommandBuffer> GetCommandBuffer() override;
    const RHIFrameContext& GetCurrentFrame() const override;

    VkInstance GetInstance() const override { return m_Instance; }
    VkPhysicalDevice GetPhysicalDevice() const override { return m_PhysicalDevice; }
    VkDevice GetDevice() const override { return m_Device; }
    VkQueue GetGraphicsQueue() const override { return m_Queue; }
    uint32_t GetGraphicsQueueFamily() const override { return m_QueueFamily; }
    VkCommandBuffer GetCurrentVkCommandBuffer() const override
    {
        return m_FrameActive ? m_CommandBuffer : VK_NULL_HANDLE;
    }
    void BeginPresentPass() override;
    void EndPresentPass() override;
    VkRenderPass GetImGuiRenderPass() const override { return m_RenderPass; }
    VkDescriptorPool GetImGuiDescriptorPool() const override { return m_DescriptorPool; }
    uint32_t GetMinImageCount() const override { return m_MinImageCount; }
    uint32_t GetImageCount() const override { return static_cast<uint32_t>(m_SwapchainImages.size()); }
    uint64_t GetSwapchainGeneration() const override { return m_SwapchainGeneration; }
    bool SupportsDynamicRendering() const override { return m_DynamicRenderingSupported; }

private:
    GLFWwindow* m_Window = nullptr;
    Ref<RHISwapChain> m_SwapChain;
    Ref<RHICommandBuffer> m_RHICommandBuffer;
    RHIFrameContext m_FrameContext;
    bool m_Initialized = false;
    bool m_FrameActive = false;
    bool m_PresentPassActive = false;
    bool m_PresentPassRecorded = false;
    bool m_FramebufferResized = false;
    bool m_DynamicRenderingSupported = false;
    uint64_t m_SwapchainGeneration = 0;

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_Queue = VK_NULL_HANDLE;
    VkExtent2D m_Extent{};

    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_SwapchainImages;
    std::vector<VkImageView> m_SwapchainImageViews;
    uint32_t m_MinImageCount = 2;

    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_Framebuffers;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;
    VkSemaphore m_CurrentRenderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence m_InFlightFence = VK_NULL_HANDLE;
    uint32_t m_QueueFamily = 0;

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createRenderPass();
    void createFramebuffers();
    void createDescriptorPool();
    void createCommandAndSyncObjects();
    void cleanupSwapchain();
    void recreateSwapchain();
};
