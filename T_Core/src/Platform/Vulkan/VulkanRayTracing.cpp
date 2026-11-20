#include "VulkanRayTracing.h"

#include "Debug/Debug.h"
#include "Platform/RHI/RHIContext.h"
#include "Platform/RHI/RHIVulkanContext.h"
#include "Platform/RenderAPIConfig.h"
#include "Platform/Vulkan/VulkanDeletionQueue.h"
#include "Scene/BVH.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace
{
	RHIVulkanContext* Context()
	{
		auto& context = RHIContext::Get();
		return context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
	}

	// KHR 扩展入口不由 vulkan-1.lib 导出，按设备加载。
	struct RayTracingFunctions
	{
		VkDevice device = VK_NULL_HANDLE;
		PFN_vkCreateAccelerationStructureKHR createAS = nullptr;
		PFN_vkDestroyAccelerationStructureKHR destroyAS = nullptr;
		PFN_vkGetAccelerationStructureBuildSizesKHR getBuildSizes = nullptr;
		PFN_vkCmdBuildAccelerationStructuresKHR cmdBuild = nullptr;
		PFN_vkGetAccelerationStructureDeviceAddressKHR getASAddress = nullptr;
		PFN_vkGetBufferDeviceAddress getBufferAddress = nullptr;

		bool Load(VkDevice d)
		{
			if (device == d && createAS)
				return true;
			device = d;
			createAS = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
				vkGetDeviceProcAddr(d, "vkCreateAccelerationStructureKHR"));
			destroyAS = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
				vkGetDeviceProcAddr(d, "vkDestroyAccelerationStructureKHR"));
			getBuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
				vkGetDeviceProcAddr(d, "vkGetAccelerationStructureBuildSizesKHR"));
			cmdBuild = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
				vkGetDeviceProcAddr(d, "vkCmdBuildAccelerationStructuresKHR"));
			getASAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
				vkGetDeviceProcAddr(d, "vkGetAccelerationStructureDeviceAddressKHR"));
			getBufferAddress = reinterpret_cast<PFN_vkGetBufferDeviceAddress>(
				vkGetDeviceProcAddr(d, "vkGetBufferDeviceAddress"));
			return createAS && destroyAS && getBuildSizes && cmdBuild && getASAddress && getBufferAddress;
		}
	};
	RayTracingFunctions g_RT;

	VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
	{
		return alignment > 1 ? (value + alignment - 1) / alignment * alignment : value;
	}

	uint32_t FindMemoryType(VkPhysicalDevice physical, uint32_t typeBits, VkMemoryPropertyFlags properties)
	{
		VkPhysicalDeviceMemoryProperties memory{};
		vkGetPhysicalDeviceMemoryProperties(physical, &memory);
		for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
		{
			if ((typeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & properties) == properties)
				return i;
		}
		throw std::runtime_error("No compatible Vulkan memory type for ray tracing buffer");
	}

	using DeviceBuffer = VulkanRayTracingScene::DeviceBuffer;

	// 带 device address 的 buffer（VulkanBuffer 不分配 DEVICE_ADDRESS 内存，这里单独实现）。
	DeviceBuffer CreateDeviceBuffer(VkDevice device, VkPhysicalDevice physical, VkDeviceSize size,
		VkBufferUsageFlags usage, bool hostVisible)
	{
		DeviceBuffer out;
		out.size = std::max<VkDeviceSize>(size, 16);

		VkBufferCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		info.size = out.size;
		info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(device, &info, nullptr, &out.buffer) != VK_SUCCESS)
			throw std::runtime_error("Failed to create ray tracing buffer");

		VkMemoryRequirements requirements{};
		vkGetBufferMemoryRequirements(device, out.buffer, &requirements);

		VkMemoryAllocateFlagsInfo flags{};
		flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

		VkMemoryAllocateInfo alloc{};
		alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc.pNext = &flags;
		alloc.allocationSize = requirements.size;
		alloc.memoryTypeIndex = FindMemoryType(physical, requirements.memoryTypeBits, hostVisible
			? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (vkAllocateMemory(device, &alloc, nullptr, &out.memory) != VK_SUCCESS)
		{
			vkDestroyBuffer(device, out.buffer, nullptr);
			throw std::runtime_error("Failed to allocate ray tracing buffer memory");
		}
		vkBindBufferMemory(device, out.buffer, out.memory, 0);
		if (hostVisible)
			vkMapMemory(device, out.memory, 0, out.size, 0, &out.mapped);

		VkBufferDeviceAddressInfo address{};
		address.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		address.buffer = out.buffer;
		out.address = g_RT.getBufferAddress(device, &address);
		return out;
	}

	void DestroyDeferred(VkDevice device, DeviceBuffer& buffer)
	{
		if (buffer.buffer == VK_NULL_HANDLE && buffer.memory == VK_NULL_HANDLE)
			return;
		// vkFreeMemory implicitly unmaps.
		VulkanDeletionQueue::EnqueueBuffer(device, buffer.buffer, buffer.memory);
		buffer = DeviceBuffer{};
	}

	VkTransformMatrixKHR ToVkTransform(const glm::mat4& m)
	{
		// VkTransformMatrixKHR is row-major 3x4; glm is column-major.
		VkTransformMatrixKHR t{};
		for (int row = 0; row < 3; ++row)
			for (int col = 0; col < 4; ++col)
				t.matrix[row][col] = m[col][row];
		return t;
	}
}

#ifdef RenderAPI_Vulkan
bool RHIRayTracingScene::IsSupported()
{
	auto* context = Context();
	return context && context->SupportsRayQuery();
}

std::string RHIRayTracingScene::GetUnsupportedReason()
{
	auto* context = Context();
	if (!context)
		return "no active Vulkan context";
	return std::string("Vulkan ray query ") + context->GetRayQueryStatus();
}

Ref<RHIRayTracingScene> RHIRayTracingScene::Create()
{
	if (!IsSupported())
		return nullptr;
	try
	{
		return CreateRef<VulkanRayTracingScene>();
	}
	catch (const std::exception& e)
	{
		Error_Core("[RayTracing] {}", e.what());
		return nullptr;
	}
}
#endif

VulkanRayTracingScene::VulkanRayTracingScene()
{
	auto* context = Context();
	if (!context || !context->SupportsRayQuery())
		throw std::runtime_error("VulkanRayTracingScene requires a device with ray query enabled");
	m_Device = context->GetDevice();
	m_PhysicalDevice = context->GetPhysicalDevice();
	m_Queue = context->GetGraphicsQueue();
	m_QueueFamily = context->GetGraphicsQueueFamily();
	if (!g_RT.Load(m_Device))
		throw std::runtime_error("Failed to load VK_KHR_acceleration_structure entry points");
}

VulkanRayTracingScene::~VulkanRayTracingScene()
{
	if (!VulkanDeletionQueue::IsClosed())
		Clear();
}

void VulkanRayTracingScene::Retire(std::vector<VkAccelerationStructureKHR> handles,
	std::vector<DeviceBuffer> storage)
{
	const VkDevice device = m_Device;
	if (device == VK_NULL_HANDLE)
		return;
	if (!handles.empty())
	{
		auto destroy = g_RT.destroyAS;
		VulkanDeletionQueue::EnqueueCallback([device, handles, destroy]()
		{
			for (VkAccelerationStructureKHR handle : handles)
				destroy(device, handle, nullptr);
		});
	}
	// Storage buffers after the AS objects (same flush; callbacks run first).
	for (DeviceBuffer& buffer : storage)
		DestroyDeferred(device, buffer);
}

void VulkanRayTracingScene::Clear()
{
	std::vector<VkAccelerationStructureKHR> handles;
	std::vector<DeviceBuffer> storage;
	for (auto& [key, blas] : m_BLASCache)
	{
		handles.push_back(blas.handle);
		storage.push_back(blas.storage);
	}
	m_BLASCache.clear();
	if (m_TLAS != VK_NULL_HANDLE)
		handles.push_back(m_TLAS);
	storage.push_back(m_TLASStorage);
	m_TLAS = VK_NULL_HANDLE;
	m_TLASStorage = DeviceBuffer{};
	Retire(std::move(handles), std::move(storage));
}

bool VulkanRayTracingScene::Update(const BVHSceneData& g, const std::vector<uint64_t>& blasKeys,
	const std::vector<BVHBLASHint>& hints)
{
	if (g.triangles.empty() || g.instances.empty())
	{
		Error_Core("[RayTracing] Update called without flat geometry");
		return false;
	}
	if (blasKeys.size() != g.blas.size() || (!hints.empty() && hints.size() != g.blas.size()))
	{
		Error_Core("[RayTracing] {} BLAS keys / {} hints for {} meshes", blasKeys.size(), hints.size(), g.blas.size());
		return false;
	}
	const auto hintOf = [&](uint32_t mesh) { return hints.empty() ? BVHBLASHint{} : hints[mesh]; };

	const auto cpuStart = std::chrono::steady_clock::now();

	VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties{};
	asProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
	VkPhysicalDeviceProperties2 properties{};
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties.pNext = &asProperties;
	vkGetPhysicalDeviceProperties2(m_PhysicalDevice, &properties);
	const VkDeviceSize scratchAlign = std::max<VkDeviceSize>(asProperties.minAccelerationStructureScratchOffsetAlignment, 1);

	// 本次新建的 BLAS（缓存里没有的键）
	struct BlasBuild
	{
		uint32_t meshIndex = 0;
		uint64_t key = 0;
		uint64_t updateFrom = 0;   // 非 0：对缓存里这个键的 BLAS 原地 UPDATE（result 即该缓存条目）
		bool allowUpdate = false;
		VkAccelerationStructureGeometryKHR geometry{};
		VkAccelerationStructureBuildGeometryInfoKHR info{};
		VkAccelerationStructureBuildRangeInfoKHR range{};
		VkDeviceSize vertexOffset = 0, asSize = 0, scratchOffset = 0;
		CachedBLAS result;
	};
	std::vector<BlasBuild> builds;

	DeviceBuffer vertices, instancesBuffer, scratch, tlasStorage;
	VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
	VkCommandPool pool = VK_NULL_HANDLE;
	VkQueryPool queries = VK_NULL_HANDLE;
	RHIRayTracingBuildStats stats;

	const auto cleanupTemporaries = [&]()
	{
		if (pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(m_Device, pool, nullptr);
		if (queries != VK_NULL_HANDLE)
			vkDestroyQueryPool(m_Device, queries, nullptr);
		pool = VK_NULL_HANDLE;
		queries = VK_NULL_HANDLE;
		DestroyDeferred(m_Device, vertices);
		DestroyDeferred(m_Device, instancesBuffer);
		DestroyDeferred(m_Device, scratch);
	};

	std::unordered_map<uint64_t, uint32_t> pending;   // key → builds 下标（同键只建一次）
	try
	{
		// ---- 1. 找出需要构建的 BLAS；顶点输入只含这些网格的平铺三角形 ----
		VkDeviceSize vertexBytes = 0;
		for (uint32_t mesh = 0; mesh < g.blas.size(); ++mesh)
		{
			const BVHBLASRange& r = g.blas[mesh];
			if (r.triangleCount == 0 || m_BLASCache.count(blasKeys[mesh]) || pending.count(blasKeys[mesh]))
				continue;
			BlasBuild b;
			b.meshIndex = mesh;
			b.key = blasKeys[mesh];
			const BVHBLASHint hint = hintOf(mesh);
			b.allowUpdate = hint.dynamic;
			if (hint.refitFrom != 0)
			{
				// UPDATE 要求：旧 BLAS 带 ALLOW_UPDATE 构建、图元数相同；否则退回完整构建
				const auto old = m_BLASCache.find(hint.refitFrom);
				if (old != m_BLASCache.end() && old->second.allowUpdate && old->second.primitiveCount == r.triangleCount)
				{
					b.updateFrom = hint.refitFrom;
					b.allowUpdate = true;
					b.result = old->second;
				}
			}
			b.vertexOffset = vertexBytes;
			vertexBytes += VkDeviceSize(r.triangleCount) * sizeof(GPUBVHTriangle);
			pending.emplace(b.key, static_cast<uint32_t>(builds.size()));
			builds.push_back(b);
		}
		if (!builds.empty())
		{
			vertices = CreateDeviceBuffer(m_Device, m_PhysicalDevice, vertexBytes,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
			for (const BlasBuild& b : builds)
			{
				const BVHBLASRange& r = g.blas[b.meshIndex];
				std::memcpy(static_cast<char*>(vertices.mapped) + b.vertexOffset, g.triangles.data() + r.triangleOffset,
					size_t(r.triangleCount) * sizeof(GPUBVHTriangle));
			}
		}

		// ---- 2. BLAS 尺寸 / 各自独立的存储（可单独释放，其余 BLAS 不受影响）----
		VkDeviceSize blasScratchBytes = 0;
		for (BlasBuild& b : builds)
		{
			const BVHBLASRange& r = g.blas[b.meshIndex];
			b.geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
			b.geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			b.geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
			auto& tri = b.geometry.geometry.triangles;
			tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			tri.vertexData.deviceAddress = vertices.address + b.vertexOffset;
			tri.vertexStride = sizeof(glm::vec4);
			tri.maxVertex = r.triangleCount * 3 - 1;
			tri.indexType = VK_INDEX_TYPE_NONE_KHR;

			b.info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
			b.info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			// UPDATE 的 flags 必须与原构建一致：动态网格始终 ALLOW_UPDATE | PREFER_FAST_TRACE
			b.info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
				(b.allowUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0);
			b.info.mode = b.updateFrom ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
				: VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			b.info.geometryCount = 1;
			b.info.pGeometries = &b.geometry;   // builds 不再扩容
			b.range.primitiveCount = r.triangleCount;

			VkAccelerationStructureBuildSizesInfoKHR sizes{};
			sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
			g_RT.getBuildSizes(m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
				&b.info, &b.range.primitiveCount, &sizes);
			b.scratchOffset = AlignUp(blasScratchBytes, scratchAlign);
			blasScratchBytes = b.scratchOffset + (b.updateFrom ? sizes.updateScratchSize : sizes.buildScratchSize);
			if (b.updateFrom)
				continue;   // 原地更新：沿用旧 BLAS 的存储

			b.asSize = sizes.accelerationStructureSize;
			b.result.storage = CreateDeviceBuffer(m_Device, m_PhysicalDevice, b.asSize,
				VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
			b.result.bytes = b.asSize;
			b.result.primitiveCount = r.triangleCount;
			b.result.allowUpdate = b.allowUpdate;
		}

		// ---- 3. TLAS 尺寸 ----
		const uint32_t instanceCount = g.stats.instanceCount;   // instances[] 可能只有一个占位
		std::vector<VkAccelerationStructureInstanceKHR> vkInstances(std::max(instanceCount, 1u));
		std::memset(vkInstances.data(), 0, vkInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));

		VkAccelerationStructureGeometryKHR tlasGeometry{};
		tlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		tlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		tlasGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		tlasGeometry.geometry.instances.arrayOfPointers = VK_FALSE;

		VkAccelerationStructureBuildGeometryInfoKHR tlasInfo{};
		tlasInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		tlasInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		tlasInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		tlasInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		tlasInfo.geometryCount = 1;
		tlasInfo.pGeometries = &tlasGeometry;

		VkAccelerationStructureBuildSizesInfoKHR tlasSizes{};
		tlasSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		g_RT.getBuildSizes(m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&tlasInfo, &instanceCount, &tlasSizes);

		tlasStorage = CreateDeviceBuffer(m_Device, m_PhysicalDevice, tlasSizes.accelerationStructureSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
		// BLAS 与 TLAS 之间有屏障，TLAS 复用同一块 scratch。额外 scratchAlign 用于把起点对齐。
		const VkDeviceSize scratchBytes = std::max(blasScratchBytes, tlasSizes.buildScratchSize) + scratchAlign;
		scratch = CreateDeviceBuffer(m_Device, m_PhysicalDevice, scratchBytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
		const VkDeviceAddress scratchBase = AlignUp(scratch.address, scratchAlign);

		// ---- 4. 创建新 BLAS 对象、取地址 ----
		for (BlasBuild& b : builds)
		{
			b.info.scratchData.deviceAddress = scratchBase + b.scratchOffset;
			if (b.updateFrom)
			{
				// src == dst：规范允许 UPDATE 原地进行。地址不变，TLAS 照常引用
				b.info.srcAccelerationStructure = b.result.handle;
				b.info.dstAccelerationStructure = b.result.handle;
				continue;
			}
			VkAccelerationStructureCreateInfoKHR create{};
			create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
			create.buffer = b.result.storage.buffer;
			create.size = b.asSize;
			create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			if (g_RT.createAS(m_Device, &create, nullptr, &b.result.handle) != VK_SUCCESS)
				throw std::runtime_error("vkCreateAccelerationStructureKHR (BLAS) failed");
			b.info.dstAccelerationStructure = b.result.handle;

			VkAccelerationStructureDeviceAddressInfoKHR address{};
			address.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
			address.accelerationStructure = b.result.handle;
			b.result.address = g_RT.getASAddress(m_Device, &address);
		}

		// ---- 5. 实例：triangleOffset → 网格 → 键 → BLAS 地址（缓存或新建）----
		std::unordered_map<uint32_t, uint32_t> meshOfTriangleOffset;
		for (uint32_t mesh = 0; mesh < g.blas.size(); ++mesh)
			if (g.blas[mesh].triangleCount > 0)
				meshOfTriangleOffset[g.blas[mesh].triangleOffset] = mesh;
		const auto blasAddress = [&](uint64_t key) -> VkDeviceAddress
		{
			if (const auto cached = m_BLASCache.find(key); cached != m_BLASCache.end())
				return cached->second.address;
			if (const auto fresh = pending.find(key); fresh != pending.end())
				return builds[fresh->second].result.address;
			throw std::runtime_error("instance references a mesh without a BLAS");
		};
		for (uint32_t i = 0; i < instanceCount; ++i)
		{
			const GPUBVHInstance& src = g.instances[i];
			const auto found = meshOfTriangleOffset.find(src.triangleOffset);
			if (found == meshOfTriangleOffset.end())
				throw std::runtime_error("instance references an unknown mesh");
			VkAccelerationStructureInstanceKHR& dst = vkInstances[i];
			dst.transform = ToVkTransform(src.objectToWorld);
			dst.instanceCustomIndex = i & 0xFFFFFFu;
			dst.mask = 0xFF;
			dst.instanceShaderBindingTableRecordOffset = 0;
			dst.flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR |
				VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
			dst.accelerationStructureReference = blasAddress(blasKeys[found->second]);
		}
		instancesBuffer = CreateDeviceBuffer(m_Device, m_PhysicalDevice,
			vkInstances.size() * sizeof(VkAccelerationStructureInstanceKHR),
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
		std::memcpy(instancesBuffer.mapped, vkInstances.data(),
			vkInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));
		tlasGeometry.geometry.instances.data.deviceAddress = instancesBuffer.address;   // 16 字节对齐

		VkAccelerationStructureCreateInfoKHR tlasCreate{};
		tlasCreate.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		tlasCreate.buffer = tlasStorage.buffer;
		tlasCreate.size = tlasSizes.accelerationStructureSize;
		tlasCreate.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		if (g_RT.createAS(m_Device, &tlasCreate, nullptr, &tlas) != VK_SUCCESS)
			throw std::runtime_error("vkCreateAccelerationStructureKHR (TLAS) failed");
		tlasInfo.dstAccelerationStructure = tlas;
		tlasInfo.scratchData.deviceAddress = scratchBase;
		VkAccelerationStructureBuildRangeInfoKHR tlasRange{};
		tlasRange.primitiveCount = instanceCount;

		// ---- 6. 录制 + 提交 ----
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		poolInfo.queueFamilyIndex = m_QueueFamily;
		if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
			throw std::runtime_error("Failed to create AS build command pool");
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = pool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(m_Device, &allocInfo, &cmd) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate AS build command buffer");

		// 时间戳（可选）
		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &familyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(familyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &familyCount, families.data());
		const bool timestamps = m_QueueFamily < familyCount && families[m_QueueFamily].timestampValidBits > 0;
		if (timestamps)
		{
			VkQueryPoolCreateInfo queryInfo{};
			queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			queryInfo.queryCount = 3;
			vkCreateQueryPool(m_Device, &queryInfo, nullptr, &queries);
		}

		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &begin);
		if (queries != VK_NULL_HANDLE)
		{
			vkCmdResetQueryPool(cmd, queries, 0, 3);
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries, 0);
		}

		VkMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		if (!builds.empty())
		{
			// 原地 UPDATE 改写的 BLAS 可能正被之前提交的帧读取（同一队列）：先等这些读取结束
			const bool anyUpdate = std::any_of(builds.begin(), builds.end(), [](const BlasBuild& b) { return b.updateFrom != 0; });
			if (anyUpdate)
			{
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
					VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
					VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
					0, 1, &barrier, 0, nullptr, 0, nullptr);
			}
			std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
			std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ranges;
			infos.reserve(builds.size());
			ranges.reserve(builds.size());
			for (const BlasBuild& b : builds)
			{
				infos.push_back(b.info);
				ranges.push_back(&b.range);
			}
			g_RT.cmdBuild(cmd, static_cast<uint32_t>(infos.size()), infos.data(), ranges.data());

			// BLAS 写完 → TLAS 构建读取 BLAS、复用 scratch
			barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
				VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
				VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
				0, 1, &barrier, 0, nullptr, 0, nullptr);
		}
		if (queries != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, queries, 1);

		const VkAccelerationStructureBuildRangeInfoKHR* tlasRanges[] = { &tlasRange };
		g_RT.cmdBuild(cmd, 1, &tlasInfo, tlasRanges);

		// TLAS 写完 → 之后任意 compute 着色器里的 ray query 读取
		barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 1, &barrier, 0, nullptr, 0, nullptr);
		if (queries != VK_NULL_HANDLE)
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries, 2);
		vkEndCommandBuffer(cmd);

		VkSubmitInfo submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		if (vkQueueSubmit(m_Queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
			vkQueueWaitIdle(m_Queue) != VK_SUCCESS)
			throw std::runtime_error("Failed to submit acceleration structure build");

		if (queries != VK_NULL_HANDLE)
		{
			uint64_t ts[3] = {};
			if (vkGetQueryPoolResults(m_Device, queries, 0, 3, sizeof(ts), ts, sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
			{
				const double period = properties.properties.limits.timestampPeriod * 1e-6;   // ns → ms
				stats.gpuBlasMilliseconds = builds.empty() ? 0.0f : static_cast<float>((ts[1] - ts[0]) * period);
				stats.gpuTlasMilliseconds = static_cast<float>((ts[2] - ts[1]) * period);
			}
		}

		for (const BlasBuild& b : builds)
			++(b.updateFrom ? stats.blasRefitted : stats.blasBuilt);
		stats.instanceCount = instanceCount;
		stats.triangleCount = g.stats.triangleCount;
		stats.tlasBytes = tlasSizes.accelerationStructureSize;
		stats.scratchBytes = scratchBytes;
	}
	catch (const std::exception& e)
	{
		Error_Core("[RayTracing] acceleration structure update failed: {}", e.what());
		// 新建但未启用的对象：队列此时空闲或从未提交，可以直接销毁
		bool anyUpdate = false;
		for (BlasBuild& b : builds)
		{
			if (b.updateFrom)
			{
				anyUpdate = true;   // 缓存里的对象，不在这里销毁
				continue;
			}
			if (b.result.handle != VK_NULL_HANDLE)
				g_RT.destroyAS(m_Device, b.result.handle, nullptr);
			DestroyDeferred(m_Device, b.result.storage);
		}
		if (tlas != VK_NULL_HANDLE)
			g_RT.destroyAS(m_Device, tlas, nullptr);
		DestroyDeferred(m_Device, tlasStorage);
		cleanupTemporaries();
		// 原地 UPDATE 可能做了一半：旧 BLAS（以及引用它的 TLAS）内容不再可信，全部丢弃重建
		if (anyUpdate)
			Clear();
		return false;
	}

	cleanupTemporaries();

	// ---- 7. 换入：新 BLAS 进缓存，释放旧 TLAS 和不再引用的 BLAS ----
	std::vector<VkAccelerationStructureKHR> retiredHandles;
	std::vector<DeviceBuffer> retiredStorage;
	if (m_TLAS != VK_NULL_HANDLE)
		retiredHandles.push_back(m_TLAS);
	retiredStorage.push_back(m_TLASStorage);
	m_TLAS = tlas;
	m_TLASStorage = tlasStorage;

	for (BlasBuild& b : builds)
	{
		if (b.updateFrom)
			m_BLASCache.erase(b.updateFrom);   // 同一个 BLAS 对象换成新键
		m_BLASCache.emplace(b.key, b.result);
	}
	std::unordered_set<uint64_t> live;
	for (uint32_t mesh = 0; mesh < g.blas.size(); ++mesh)
		if (g.blas[mesh].triangleCount > 0)
			live.insert(blasKeys[mesh]);
	uint64_t blasBytes = 0;
	for (auto it = m_BLASCache.begin(); it != m_BLASCache.end();)
	{
		if (!live.count(it->first))
		{
			retiredHandles.push_back(it->second.handle);
			retiredStorage.push_back(it->second.storage);
			it = m_BLASCache.erase(it);
		}
		else
		{
			blasBytes += it->second.bytes;
			++it;
		}
	}
	Retire(std::move(retiredHandles), std::move(retiredStorage));

	stats.blasCount = static_cast<uint32_t>(m_BLASCache.size());
	stats.blasBytes = blasBytes;
	stats.cpuMilliseconds = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - cpuStart).count();
	m_Stats = stats;

	// 只重建 TLAS（拖动物体时每帧一次）不刷日志
	// refit（变形网格每帧一次）同样不刷日志
	if (stats.blasBuilt > 0)
		Info_Core("[RayTracing] {} BLAS resident ({} built, {} refitted, {:.1f} MB), TLAS {} instances ({:.2f} MB) | "
		"GPU BLAS {:.2f} ms, TLAS {:.2f} ms | CPU {:.1f} ms",
		stats.blasCount, stats.blasBuilt, stats.blasRefitted, stats.blasBytes / 1048576.0, stats.instanceCount,
		stats.tlasBytes / 1048576.0, stats.gpuBlasMilliseconds, stats.gpuTlasMilliseconds,
		stats.cpuMilliseconds);
	return true;
}
