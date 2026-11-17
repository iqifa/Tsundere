#include "Platform/RHI/RHIGpuTimer.h"
#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include "Platform/RenderAPIConfig.h"
#include "Platform/Vulkan/VulkanDeletionQueue.h"

#include <array>
#include <vector>
#include <vulkan/vulkan.h>

#ifdef RenderAPI_Vulkan
namespace
{
	RHIVulkanContext* Context()
	{
		auto& context = RHIContext::Get();
		return context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
	}

	// 每槽两个时间戳。槽在写入它的那一帧结束（帧栅栏）后读取，不带 WAIT 标志。
	// VulkanContext 只有一帧在途，BeginFrame 等待栅栏后上一帧的查询必然可读。
	class VulkanGpuTimer final : public RHIGpuTimer
	{
	public:
		VulkanGpuTimer(VkDevice device, float timestampPeriod)
			: m_Device(device), m_PeriodMs(static_cast<double>(timestampPeriod) * 1e-6)
		{
			VkQueryPoolCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			info.queryType = VK_QUERY_TYPE_TIMESTAMP;
			info.queryCount = static_cast<uint32_t>(kSlots * 2);
			if (vkCreateQueryPool(device, &info, nullptr, &m_Pool) != VK_SUCCESS)
				m_Pool = VK_NULL_HANDLE;
		}

		~VulkanGpuTimer() override
		{
			if (m_Pool == VK_NULL_HANDLE || VulkanDeletionQueue::IsClosed())
				return;
			const VkDevice device = m_Device;
			const VkQueryPool pool = m_Pool;
			VulkanDeletionQueue::EnqueueCallback([device, pool]() { vkDestroyQueryPool(device, pool, nullptr); });
		}

		void Begin(RHICommandBuffer&) override
		{
			m_Recording = false;
			auto* context = Context();
			auto& rhi = RHIContext::Get();
			if (m_Pool == VK_NULL_HANDLE || !context || !rhi)
				return;
			VkCommandBuffer cmd = context->GetCurrentVkCommandBuffer();
			if (cmd == VK_NULL_HANDLE)
				return;
			const uint64_t frame = rhi->GetCurrentFrame().FrameIndex;
			Harvest(frame);

			Slot& slot = m_Slots[m_Next];
			if (slot.pending)
				return;   // 还没读回：本帧不测
			const uint32_t first = static_cast<uint32_t>(m_Next * 2);
			vkCmdResetQueryPool(cmd, m_Pool, first, 2);
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_Pool, first);
			slot.frame = frame;
			m_Recording = true;
		}

		void End(RHICommandBuffer&) override
		{
			if (!m_Recording)
				return;
			auto* context = Context();
			VkCommandBuffer cmd = context ? context->GetCurrentVkCommandBuffer() : VK_NULL_HANDLE;
			if (cmd == VK_NULL_HANDLE)
				return;
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_Pool,
				static_cast<uint32_t>(m_Next * 2 + 1));
			m_Slots[m_Next].pending = true;
			m_Next = (m_Next + 1) % kSlots;
			m_Recording = false;
		}

		double GetMilliseconds() const override { return m_Last; }

	private:
		static constexpr size_t kSlots = 4;
		struct Slot
		{
			bool pending = false;
			uint64_t frame = 0;
		};

		void Harvest(uint64_t currentFrame)
		{
			for (size_t i = 0; i < kSlots; ++i)
			{
				Slot& slot = m_Slots[i];
				if (!slot.pending || slot.frame >= currentFrame)
					continue;
				uint64_t ts[2] = {};
				const VkResult result = vkGetQueryPoolResults(m_Device, m_Pool,
					static_cast<uint32_t>(i * 2), 2, sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
				if (result == VK_NOT_READY)
					continue;
				if (result == VK_SUCCESS && ts[1] >= ts[0])
					m_Last = static_cast<double>(ts[1] - ts[0]) * m_PeriodMs;
				slot.pending = false;
			}
		}

		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueryPool m_Pool = VK_NULL_HANDLE;
		double m_PeriodMs = 0.0;
		std::array<Slot, kSlots> m_Slots{};
		size_t m_Next = 0;
		bool m_Recording = false;
		double m_Last = -1.0;
	};
}

Ref<RHIGpuTimer> RHIGpuTimer::Create()
{
	auto* context = Context();
	if (!context || context->GetDevice() == VK_NULL_HANDLE)
		return nullptr;

	uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(context->GetPhysicalDevice(), &familyCount, nullptr);
	std::vector<VkQueueFamilyProperties> families(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(context->GetPhysicalDevice(), &familyCount, families.data());
	const uint32_t family = context->GetGraphicsQueueFamily();
	if (family >= familyCount || families[family].timestampValidBits == 0)
		return nullptr;

	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(context->GetPhysicalDevice(), &properties);
	return CreateRef<VulkanGpuTimer>(context->GetDevice(), properties.limits.timestampPeriod);
}
#endif
