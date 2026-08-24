#include "Platform/RHI/RHIImGuiRenderer.h"

#ifdef RenderAPI_Vulkan

#include "Platform/RHI/RHIRenderer.h"
#include "Platform/RHI/RHIVulkanContext.h"

#include <stdexcept>

namespace Vulkan_ImGui
{
    namespace
    {
        RHIVulkanContext* GetContext()
        {
            auto context = RHIRenderer::GetContext();
            auto* vulkanContext = dynamic_cast<RHIVulkanContext*>(context.get());
            if (!vulkanContext)
                throw std::runtime_error("The active RHI context is not Vulkan");
            return vulkanContext;
        }

        void CheckVkResult(VkResult result)
        {
            if (result != VK_SUCCESS)
                Error_Core("ImGui Vulkan backend error: {}", static_cast<int>(result));
        }
    }

    void Init(Engine::Application& app)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // Secondary Vulkan viewports need their own swapchains and render paths.
        // Keep them disabled until that ownership is integrated into the RHI.
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

        GLFWwindow* window =
            static_cast<GLFWwindow*>(app.GetWindow().GetWindow());
        if (!ImGui_ImplGlfw_InitForVulkan(window, true))
            throw std::runtime_error("Failed to initialize ImGui GLFW Vulkan backend");

        RHIVulkanContext* context = GetContext();

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_0;
        initInfo.Instance = context->GetInstance();
        initInfo.PhysicalDevice = context->GetPhysicalDevice();
        initInfo.Device = context->GetDevice();
        initInfo.QueueFamily = context->GetGraphicsQueueFamily();
        initInfo.Queue = context->GetGraphicsQueue();
        initInfo.DescriptorPool = context->GetImGuiDescriptorPool();
        initInfo.MinImageCount = context->GetMinImageCount();
        initInfo.ImageCount = context->GetImageCount();
        initInfo.RenderPass = context->GetImGuiRenderPass();
        initInfo.Subpass = 0;
        initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.CheckVkResultFn = CheckVkResult;

        if (!ImGui_ImplVulkan_Init(&initInfo))
        {
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
        }
    }

    void Begin(Engine::Application& app, float& timeState)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(
            static_cast<float>(app.GetWindow().GetWidth()),
            static_cast<float>(app.GetWindow().GetHeight()));

        const float time = static_cast<float>(glfwGetTime());
        io.DeltaTime = timeState > 0.0f ? time - timeState : 1.0f / 60.0f;
        timeState = time;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void End()
    {
        ImGui::Render();

        VkCommandBuffer commandBuffer = GetContext()->GetCurrentVkCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE)
        {
            Error_Core("ImGui Vulkan render skipped: no active command buffer");
            return;
        }

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }

    void Shutdown()
    {
        auto context = RHIRenderer::GetContext();
        if (context)
            context->WaitIdle();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
}

struct VulkanImGuiRegisterer
{
    VulkanImGuiRegisterer()
    {
        RHIImGUIRenderer::Register({
            Vulkan_ImGui::Init,
            Vulkan_ImGui::Begin,
            Vulkan_ImGui::End,
            Vulkan_ImGui::Shutdown
        });
    }
};

static VulkanImGuiRegisterer s_AutoRegister;

#endif // RenderAPI_Vulkan
