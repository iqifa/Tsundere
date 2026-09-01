#include "Platform/RHI/RHIImGuiRenderer.h"

#ifdef RenderAPI_Vulkan

#include "Platform/RHI/RHIRenderer.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include "Platform/Vulkan/VulkanTexture2D.h"

#include <stdexcept>
#include <unordered_map>

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

        struct TextureRegistration
        {
            VkImageView imageView = VK_NULL_HANDLE;
            VkSampler sampler = VK_NULL_HANDLE;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        };

        std::unordered_map<RHITexture2D*, TextureRegistration> s_TextureRegistrations;

        void RemoveRegistration(
            std::unordered_map<RHITexture2D*, TextureRegistration>::iterator registration)
        {
            if (registration->second.descriptorSet != VK_NULL_HANDLE)
                ImGui_ImplVulkan_RemoveTexture(registration->second.descriptorSet);
            s_TextureRegistrations.erase(registration);
        }

        void ClearTextureRegistrations()
        {
            for (const auto& registration : s_TextureRegistrations)
            {
                if (registration.second.descriptorSet != VK_NULL_HANDLE)
                    ImGui_ImplVulkan_RemoveTexture(registration.second.descriptorSet);
            }
            s_TextureRegistrations.clear();
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
        initInfo.ApiVersion = VK_API_VERSION_1_3;
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

        RHIVulkanContext* context = GetContext();
        VkCommandBuffer commandBuffer = context->GetCurrentVkCommandBuffer();
        if (commandBuffer == VK_NULL_HANDLE)
        {
            Error_Core("ImGui Vulkan render skipped: no active command buffer");
            return;
        }

        context->BeginPresentPass();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        context->EndPresentPass();
    }

    ImTextureID GetTextureID(RHITexture2D* texture)
    {
        auto* vulkanTexture = dynamic_cast<VulkanTexture2D*>(texture);
        if (!vulkanTexture)
        {
            Error_Core("ImGui Vulkan texture registration failed: texture is not Vulkan");
            return ImTextureID_Invalid;
        }

        const VkImageView imageView = vulkanTexture->GetImageView();
        const VkSampler sampler = vulkanTexture->GetSampler();
        if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
        {
            Error_Core("ImGui Vulkan texture registration failed: invalid image view or sampler");
            return ImTextureID_Invalid;
        }

        auto registration = s_TextureRegistrations.find(texture);
        if (registration != s_TextureRegistrations.end())
        {
            if (registration->second.imageView == imageView &&
                registration->second.sampler == sampler)
            {
                return reinterpret_cast<ImTextureID>(
                    registration->second.descriptorSet);
            }

            auto context = RHIRenderer::GetContext();
            if (context)
                context->WaitIdle();
            RemoveRegistration(registration);
        }

        const VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
            sampler,
            imageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (descriptorSet == VK_NULL_HANDLE)
        {
            Error_Core("ImGui Vulkan texture registration failed: descriptor allocation returned null");
            return ImTextureID_Invalid;
        }

        s_TextureRegistrations.emplace(texture, TextureRegistration{
            imageView,
            sampler,
            descriptorSet });
        return reinterpret_cast<ImTextureID>(descriptorSet);
    }

    void ReleaseTexture(RHITexture2D* texture)
    {
        auto registration = s_TextureRegistrations.find(texture);
        if (registration == s_TextureRegistrations.end())
            return;

        auto context = RHIRenderer::GetContext();
        if (context)
            context->WaitIdle();
        RemoveRegistration(registration);
    }

    void Shutdown()
    {
        auto context = RHIRenderer::GetContext();
        if (context)
            context->WaitIdle();

        ClearTextureRegistrations();
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
            Vulkan_ImGui::Shutdown,
            Vulkan_ImGui::GetTextureID,
            Vulkan_ImGui::ReleaseTexture
        });
    }
};

static VulkanImGuiRegisterer s_AutoRegister;

#endif // RenderAPI_Vulkan
