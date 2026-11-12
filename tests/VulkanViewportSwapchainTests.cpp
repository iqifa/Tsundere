#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_vulkan.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <string>

static int errors = 0;
static VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ||
        std::strstr(data->pMessage, "has not been acquired"))
    { ++errors; std::cerr << data->pMessage << "\n"; }
    return VK_FALSE;
}
static void Check(VkResult result) {
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Vulkan result " + std::to_string(result));
}
int main() {
try {
    if (!glfwInit()) throw std::runtime_error("GLFW init failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(160, 120, "Viewport swapchain regression", nullptr, nullptr);
    if (!window) throw std::runtime_error("Cannot create test window");
    uint32_t count = 0; const char** required = glfwGetRequiredInstanceExtensions(&count);
    std::vector<const char*> extensions(required, required + count); extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    const char* layer = "VK_LAYER_KHRONOS_validation";
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
    debug.pfnUserCallback = Validation;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &app;
    info.enabledLayerCount = 1; info.ppEnabledLayerNames = &layer;
    info.enabledExtensionCount = uint32_t(extensions.size()); info.ppEnabledExtensionNames = extensions.data(); info.pNext = &debug;
    VkInstance instance; Check(vkCreateInstance(&info, nullptr, &instance));
    VkDebugUtilsMessengerEXT messenger;
    Check(reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"))(instance,&debug,nullptr,&messenger));
    ImGui::CreateContext();
    ImGui_ImplVulkanH_Window wd; Check(glfwCreateWindowSurface(instance, window, nullptr, &wd.Surface));
    uint32_t deviceCount = 0; Check(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
    std::vector<VkPhysicalDevice> devices(deviceCount); Check(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));
    VkPhysicalDevice gpu = VK_NULL_HANDLE; uint32_t family = 0;
    for (auto candidate : devices) {
        uint32_t n = 0; vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
        std::vector<VkQueueFamilyProperties> queues(n); vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, queues.data());
        for (uint32_t i=0;i<n;i++) { VkBool32 present; Check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,wd.Surface,&present));
            if (present && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {gpu=candidate;family=i;break;} }
        if (gpu) break;
    }
    if (!gpu) throw std::runtime_error("No presentation device");
    float priority=1; VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
    const char* swapchain=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
    deviceInfo.enabledExtensionCount=1;deviceInfo.ppEnabledExtensionNames=&swapchain;
    VkDevice device;Check(vkCreateDevice(gpu,&deviceInfo,nullptr,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkFormat formats[]={VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM};
    wd.SurfaceFormat=ImGui_ImplVulkanH_SelectSurfaceFormat(gpu,wd.Surface,formats,2,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    wd.PresentMode=VK_PRESENT_MODE_FIFO_KHR;
    for (int resize=0;resize<3;resize++) {
        const int width=160+resize*32, height=120+resize*24;
        glfwSetWindowSize(window,width,height);glfwPollEvents();
        ImGui_ImplVulkanH_CreateOrResizeWindow(instance,gpu,device,&wd,family,nullptr,width,height,2);
        for(int frame=0;frame<4;frame++) {
            auto& sem=wd.FrameSemaphores[wd.SemaphoreIndex];
            Check(vkAcquireNextImageKHR(device,wd.Swapchain,UINT64_MAX,sem.ImageAcquiredSemaphore,VK_NULL_HANDLE,&wd.FrameIndex));
            auto& f=wd.Frames[wd.FrameIndex];Check(vkWaitForFences(device,1,&f.Fence,VK_TRUE,UINT64_MAX));
            Check(vkResetCommandPool(device,f.CommandPool,0));
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(f.CommandBuffer,&begin));
            VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};render.renderPass=wd.RenderPass;render.framebuffer=f.Framebuffer;
            render.renderArea.extent={uint32_t(wd.Width),uint32_t(wd.Height)};
            VkClearValue clear{};render.clearValueCount=1;render.pClearValues=&clear;
            vkCmdBeginRenderPass(f.CommandBuffer,&render,VK_SUBPASS_CONTENTS_INLINE);vkCmdEndRenderPass(f.CommandBuffer);
            Check(vkEndCommandBuffer(f.CommandBuffer));Check(vkResetFences(device,1,&f.Fence));
            VkPipelineStageFlags wait=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&sem.ImageAcquiredSemaphore;submit.pWaitDstStageMask=&wait;
            submit.commandBufferCount=1;submit.pCommandBuffers=&f.CommandBuffer;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&sem.RenderCompleteSemaphore;
            Check(vkQueueSubmit(queue,1,&submit,f.Fence));
            VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&sem.RenderCompleteSemaphore;
            present.swapchainCount=1;present.pSwapchains=&wd.Swapchain;present.pImageIndices=&wd.FrameIndex;Check(vkQueuePresentKHR(queue,&present));
            Check(vkQueueWaitIdle(queue));wd.SemaphoreIndex=(wd.SemaphoreIndex+1)%wd.SemaphoreCount;
        }
    }
    Check(vkDeviceWaitIdle(device));ImGui_ImplVulkanH_DestroyWindow(instance,device,&wd,nullptr);
    ImGui::DestroyContext();vkDestroyDevice(device,nullptr);
    reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,nullptr);
    vkDestroyInstance(instance,nullptr);glfwDestroyWindow(window);glfwTerminate();
    if(errors) {std::cerr<<"FAIL: "<<errors<<" validation errors\n";return 1;}
    std::cout<<"PASS: viewport swapchain creation, 2 rebuilds, 12 acquire/render/present frames; zero validation errors\n";
    return 0;
} catch (const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
