#include "Core/Window.h"
#include "Debug/Debug.h"
#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include "Platform/RHI/RHIBuffer.h"
#include "Platform/RHI/RHIMesh.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanDeletionQueue.h"
#include "Platform/Vulkan/VulkanMemoryPool.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

struct GLFWwindow;

static void Require(bool cond, const char* msg)
{
    if (!cond)
    {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main()
{
    try
    {
    Engine::Log::Init();
    std::unique_ptr<Engine::Window> win(Engine::Window::Create(
        Engine::WindowProps("VulkanMemoryPoolTests", 320, 240)));
    Require(win != nullptr, "Engine::Window::Create");
    GLFWwindow* window = static_cast<GLFWwindow*>(win->GetWindow());
    Require(window != nullptr, "win->GetWindow()");

    auto rhiContext = RHIContext::Create(window);
    Require(rhiContext != nullptr, "RHIContext::Create");
    auto* vkCtx = dynamic_cast<RHIVulkanContext*>(rhiContext.get());
    Require(vkCtx != nullptr, "RHIVulkanContext cast");
    VkDevice device = vkCtx->GetDevice();

    // 1. Sub-allocate 100 vertex + index buffers (200 buffers total) into shared pool blocks
    std::vector<Ref<RHIBuffer>> deviceBuffers;
    const uint32_t sampleData[64] = { 1, 2, 3, 4 };
    for (int i = 0; i < 100; ++i)
    {
        deviceBuffers.push_back(RHIBuffer::Create({ 256, BufferUsage::Vertex, false, sampleData }));
        deviceBuffers.push_back(RHIBuffer::Create({ 128, BufferUsage::Index, false, sampleData }));
    }

    auto* firstBuf = dynamic_cast<VulkanBuffer*>(deviceBuffers[0].get());
    auto* secondBuf = dynamic_cast<VulkanBuffer*>(deviceBuffers[2].get());
    Require(firstBuf && secondBuf, "VulkanBuffer cast");
    Require(firstBuf->GetMemory() == secondBuf->GetMemory(),
        "Multiple DEVICE_LOCAL buffers should share the same VkDeviceMemory block");
    Require(firstBuf->GetMemoryOffset() != secondBuf->GetMemoryOffset(),
        "Sub-allocated buffers should have distinct memory offsets");

    VulkanMemoryPoolStats stats = VulkanMemoryPool::GetStats(device);
    Require(stats.allocationCount >= 200, "At least 200 live allocations expected");
    Require(stats.blockCount <= 4, "200 small buffers should fit in very few VkDeviceMemory blocks");

    // 2. Test HOST_VISIBLE persistent block mapping across multiple buffers
    std::vector<Ref<RHIBuffer>> hostBuffers;
    for (int i = 0; i < 16; ++i)
    {
        auto buf = RHIBuffer::Create({ 256, BufferUsage::Uniform, true, nullptr });
        void* mapped = buf->Map();
        Require(mapped != nullptr, "HOST_VISIBLE buffer Map() must succeed");
        uint32_t tag = static_cast<uint32_t>(0xA000 + i);
        std::memcpy(mapped, &tag, sizeof(tag));
        hostBuffers.push_back(buf);
    }

    auto* host0 = dynamic_cast<VulkanBuffer*>(hostBuffers[0].get());
    auto* host1 = dynamic_cast<VulkanBuffer*>(hostBuffers[1].get());
    Require(host0->GetMemory() == host1->GetMemory(),
        "Multiple HOST_VISIBLE buffers should share the same mapped VkDeviceMemory block");

    // 3. Test VulkanMesh creation (2 buffers per mesh)
    struct TestVertex { float pos[3]; };
    std::vector<TestVertex> verts = { {{0,0,0}}, {{1,0,0}}, {{0,1,0}} };
    std::vector<uint32_t> indices = { 0, 1, 2 };
    VertexLayout layout;
    layout.stride = sizeof(TestVertex);
    layout.attributes = { { 0, VertexFormat::Float3, 0 } };
    auto mesh = RHIMesh::Create({ verts.data(), 3, indices.data(), 3, layout });
    Require(mesh && mesh->IsValid(), "RHIMesh::Create should succeed");

    // 4. Test Free + Coalescing via VulkanDeletionQueue::Flush
    const uint32_t blocksBeforeFree = VulkanMemoryPool::GetStats(device).blockCount;
    // Free two adjacent buffers in the middle and allocate a slightly larger buffer
    deviceBuffers[10].reset();
    deviceBuffers[11].reset();
    VulkanDeletionQueue::Flush();

    auto coalesced = RHIBuffer::Create({ 384, BufferUsage::Vertex, false, sampleData });
    Require(coalesced != nullptr, "Allocating into freed/coalesced space should succeed");
    Require(VulkanMemoryPool::GetStats(device).blockCount == blocksBeforeFree,
        "Coalesced allocation should reuse existing pool block");

    // Release all test resources before shutdown
    coalesced.reset();
    mesh.reset();
    hostBuffers.clear();
    deviceBuffers.clear();
    VulkanDeletionQueue::Flush();

    rhiContext->Shutdown();
    rhiContext.reset();
    RHIContext::Get().reset();
    win.reset();

    VulkanMemoryPoolStats postShutdown = VulkanMemoryPool::GetStats(device);
    Require(postShutdown.blockCount == 0 && postShutdown.allocationCount == 0,
        "All pool blocks must be freed on Shutdown");

    std::cout << "PASS: VulkanMemoryPoolTests (200+ buffers shared "
              << stats.blockCount << " VkDeviceMemory block(s))\n";
    return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return 1;
    }
}
