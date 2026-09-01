#include "VulkanPipeline.h"
#include<Platform/Vulkan/VulkanShader.h>
#include<Platform/Vulkan/VulkanDescriptorSet.h>
#include<Platform/RHI/RHIContext.h>
#include<Platform/RHI/RHIVulkanContext.h>
#include "Platform/RenderAPIConfig.h"

#ifdef RenderAPI_Vulkan
Ref<RHIPipeline> RHIPipeline::Create(const PipelineDesc& desc)
{
    return CreateRef<VulkanPipeline>(desc);
}
#endif

#include<Debug/Debug.h>
namespace VulkanPipelineUtil {
	VkFormat ToVKVertexType(VertexFormat fmt)
	{
		switch (fmt)
		{
		case VertexFormat::Float:		return VK_FORMAT_R32_SFLOAT;
		case VertexFormat::Float2:		return VK_FORMAT_R32G32_SFLOAT;
		case VertexFormat::Float3:		return VK_FORMAT_R32G32B32_SFLOAT;
		case VertexFormat::Float4:		return VK_FORMAT_R32G32B32A32_SFLOAT;
		case VertexFormat::Int:			return VK_FORMAT_R32_SINT;
		case VertexFormat::Int2:		return VK_FORMAT_R32G32_SINT;
		case VertexFormat::Int3:		return VK_FORMAT_R32G32B32_SINT;
		case VertexFormat::Int4:		return VK_FORMAT_R32G32B32A32_SINT;
		case VertexFormat::UByte4Norm:	return VK_FORMAT_R8G8B8A8_UNORM;
		default:
			break;
		}
		return VK_FORMAT_UNDEFINED;
	}
	VkFormat ToVkFormat(Format format)
	{
		switch (format)
		{
		case Format::R8_UNORM:           return VK_FORMAT_R8_UNORM;
		case Format::RGBA8_UNORM:        return VK_FORMAT_R8G8B8A8_UNORM;
		case Format::RGBA8_SRGB:         return VK_FORMAT_R8G8B8A8_SRGB;
		case Format::R16F:               return VK_FORMAT_R16_SFLOAT;
		case Format::RG16F:              return VK_FORMAT_R16G16_SFLOAT;
		case Format::RGBA16F:             return VK_FORMAT_R16G16B16A16_SFLOAT;
		case Format::RGBA32F:             return VK_FORMAT_R32G32B32A32_SFLOAT;
		case Format::D24_UNORM_S8_UINT:  return VK_FORMAT_D24_UNORM_S8_UINT;
		case Format::D32_SFLOAT:         return VK_FORMAT_D32_SFLOAT;
		case Format::D32_SFLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
		case Format::R32_UINT:            return VK_FORMAT_R32_UINT;
		case Format::R32_SINT:            return VK_FORMAT_R32_SINT;
		case Format::Unknown:             break;
		}
		Error_Core("Unsupported Vulkan format: {0}", static_cast<uint8_t>(format));
		return VK_FORMAT_UNDEFINED;
	}
			Format ResolveDepthFormat(Format requested)
		{
			if (requested != Format::D24_UNORM_S8_UINT && requested != Format::D32_SFLOAT && requested != Format::D32_SFLOAT_S8_UINT)
				return requested;

			auto& context = RHIContext::Get();
			auto* vkContext = context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
			if (!vkContext || vkContext->GetPhysicalDevice() == VK_NULL_HANDLE)
				return requested;

			const Format candidates[] = { requested, Format::D32_SFLOAT_S8_UINT, Format::D32_SFLOAT, Format::D24_UNORM_S8_UINT };
			for (Format candidate : candidates)
			{
				VkFormatProperties properties{};
				vkGetPhysicalDeviceFormatProperties(vkContext->GetPhysicalDevice(), ToVkFormat(candidate), &properties);
				if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
					return candidate;
			}

			Error_Core("No Vulkan depth attachment format is supported");
			return Format::Unknown;
		}
		VkSampleCountFlagBits ToVkSampleCount(uint32_t sampleCount)
	{
		switch (sampleCount)
		{
		case 1: return VK_SAMPLE_COUNT_1_BIT;
		case 2: return VK_SAMPLE_COUNT_2_BIT;
		case 4: return VK_SAMPLE_COUNT_4_BIT;
		case 8: return VK_SAMPLE_COUNT_8_BIT;
		default:
			Error_Core("Unsupported Vulkan sample count: {0}", sampleCount);
			return static_cast<VkSampleCountFlagBits>(0);
		}
	}
	VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology)
	{
		switch (topology)
		{
		case PrimitiveTopology::Triangles:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case PrimitiveTopology::TriangleStrip:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case PrimitiveTopology::Lines:
			return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case PrimitiveTopology::Points:
			return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		}

		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}
	VkCullModeFlags ToVKCullMode(CullMode cullmode)
	{
		switch (cullmode)
		{
		case CullMode::None:
			return VK_CULL_MODE_NONE;
		case CullMode::Front:
			return VK_CULL_MODE_FRONT_BIT;
		case CullMode::Back:
			return VK_CULL_MODE_BACK_BIT;
		case CullMode::Double:
			return VK_CULL_MODE_FRONT_AND_BACK;
		default:
			Error_Core("UnSupport CullMode:[{0}],Altinate with Back Cull", (uint8_t)cullmode);
		}
		return VK_CULL_MODE_BACK_BIT;

	}
	VkCompareOp ToVkCompareOp(CompareOp op)
	{
		switch (op)
		{
		case CompareOp::Never:
			return VK_COMPARE_OP_NEVER;
		case CompareOp::Less:
			return VK_COMPARE_OP_LESS;
		case CompareOp::Equal:
			return VK_COMPARE_OP_EQUAL;
		case CompareOp::LessEqual:
			return VK_COMPARE_OP_LESS_OR_EQUAL;
		case CompareOp::Greater:
			return VK_COMPARE_OP_GREATER;
		case CompareOp::NotEqual:
			return VK_COMPARE_OP_NOT_EQUAL;
		case CompareOp::GreaterEqual:
			return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case CompareOp::Always:
			return VK_COMPARE_OP_ALWAYS;
		}

		return VK_COMPARE_OP_LESS;
	}
	VkBlendFactor ToVkBlendFactor(BlendFactor factor)
	{
		switch (factor)
		{
		case BlendFactor::Zero:
			return VK_BLEND_FACTOR_ZERO;
		case BlendFactor::One:
			return VK_BLEND_FACTOR_ONE;
		case BlendFactor::SrcAlpha:
			return VK_BLEND_FACTOR_SRC_ALPHA;
		case BlendFactor::OneMinusSrcAlpha:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		}

		return VK_BLEND_FACTOR_ONE;
	}
}

VulkanPipeline::VulkanPipeline(const PipelineDesc& desc) : m_Desc(desc)
{
	if (desc.isCompute)
	{
		Error_Core("VulkanPipeline: compute pipelines require a dedicated creation path");
		return;
	}

	if (!desc.renderingSignature.HasAttachments())
	{
		Error_Core("VulkanPipeline: graphics pipeline has no rendering signature");
		return;
	}

	if (!desc.shader)
	{
		Error_Core("VulkanPipeline: graphics pipeline requires a shader");
		return;
	}

#pragma region VulkanShaderInfo
	auto& shader = desc.shader;
	auto* vulkan_shader = dynamic_cast<VulkanShader*>(shader.get());
	if (!vulkan_shader)
	{
		Error_Core("VulkanPipeline: shader was not created by the Vulkan backend");
		return;
	}

	auto [vert, frag] = vulkan_shader->GetGraphicsShaderModule();
	if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE)
	{
		Error_Core("VulkanPipeline: graphics shader is missing a vertex or fragment module");
		return;
	}

	VkPipelineShaderStageCreateInfo vertStageInfo{};
	vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertStageInfo.module = vert;
	vertStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragStageInfo{};
	fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragStageInfo.module = frag;
	fragStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };
#pragma endregion

	
#pragma region VertexLayout
	VkVertexInputBindingDescription bindingDescription{};
	bindingDescription.binding = 0;
	bindingDescription.stride = desc.vertexLayout.stride;
	bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;



	std::vector<VkVertexInputAttributeDescription>
		attributeDescriptions;

	attributeDescriptions.reserve(
		desc.vertexLayout.attributes.size());

	for (const VertexAttribute& attribute :
		desc.vertexLayout.attributes)
	{
		VkVertexInputAttributeDescription vkAttribute{};
		vkAttribute.location = attribute.location;
		vkAttribute.binding = attribute.binding;
		vkAttribute.format =
			(VkFormat)VulkanPipelineUtil::ToVKVertexType(attribute.format);
		vkAttribute.offset = attribute.offset;

		attributeDescriptions.push_back(vkAttribute);
	}

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType =
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	if (!desc.vertexLayout.attributes.empty())
	{
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions =
			&bindingDescription;

		vertexInputInfo.vertexAttributeDescriptionCount =
			static_cast<uint32_t>(
				attributeDescriptions.size());

		vertexInputInfo.pVertexAttributeDescriptions =
			attributeDescriptions.data();
	}
#pragma endregion


#pragma region Poly
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType =
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology =
		(VkPrimitiveTopology)VulkanPipelineUtil::ToVkTopology(desc.topology);
	inputAssembly.primitiveRestartEnable = VK_FALSE;
#pragma endregion

#pragma region ViewPort
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType =
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	const VkDynamicState dynamicStates[] = {
	VK_DYNAMIC_STATE_VIEWPORT,
	VK_DYNAMIC_STATE_SCISSOR
	};


	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType =
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount =
		static_cast<uint32_t>(std::size(dynamicStates));
	dynamicState.pDynamicStates = dynamicStates;
#pragma endregion

#pragma region Rasterization
	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType =
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;

	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;

	rasterizer.cullMode = 
		VulkanPipelineUtil::ToVKCullMode(desc.cullMode);
	rasterizer.frontFace =
		VK_FRONT_FACE_COUNTER_CLOCKWISE;

	rasterizer.depthBiasEnable = VK_FALSE;
	rasterizer.depthBiasConstantFactor = 0.0f;
	rasterizer.depthBiasClamp = 0.0f;
	rasterizer.depthBiasSlopeFactor = 0.0f;
#pragma endregion

#pragma region Multisample State
	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples =
		VulkanPipelineUtil::ToVkSampleCount(
			desc.renderingSignature.sampleCount);
	if (multisampling.rasterizationSamples == 0)
	{
		Error_Core("VulkanPipeline: invalid rendering signature sample count");
		return;
	}

	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = nullptr;
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;
#pragma endregion

#pragma region DepthState
	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

	depthStencil.depthTestEnable =
		desc.depthTest ? VK_TRUE : VK_FALSE;

	depthStencil.depthWriteEnable =
		desc.depthWrite ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp =
		(VkCompareOp)VulkanPipelineUtil::ToVkCompareOp(desc.depthOp);
#pragma endregion

#pragma region BlendState
	const bool blendEnabled =
		desc.srcBlend != BlendFactor::One ||
		desc.dstBlend != BlendFactor::Zero;

	// Create one blend attachment state per color attachment
	uint32_t colorAttachmentCount =
		static_cast<uint32_t>(desc.renderingSignature.colorFormats.size());

	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorAttachmentCount);

	for (auto& colorBlendAttachment : colorBlendAttachments)
	{
		colorBlendAttachment.blendEnable =
			blendEnabled ? VK_TRUE : VK_FALSE;

		colorBlendAttachment.srcColorBlendFactor =
			(VkBlendFactor)VulkanPipelineUtil::ToVkBlendFactor(desc.srcBlend);

		colorBlendAttachment.dstColorBlendFactor =
			(VkBlendFactor)VulkanPipelineUtil::ToVkBlendFactor(desc.dstBlend);

		colorBlendAttachment.colorBlendOp =
			VK_BLEND_OP_ADD;

		colorBlendAttachment.srcAlphaBlendFactor =
			(VkBlendFactor)VulkanPipelineUtil::ToVkBlendFactor(desc.srcBlend);

		colorBlendAttachment.dstAlphaBlendFactor =
			(VkBlendFactor)VulkanPipelineUtil::ToVkBlendFactor(desc.dstBlend);

		colorBlendAttachment.alphaBlendOp =
			VK_BLEND_OP_ADD;

		colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT;
	}


	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;

	colorBlending.attachmentCount = colorAttachmentCount;
	colorBlending.pAttachments =
		colorBlendAttachments.empty()
			? nullptr
			: colorBlendAttachments.data();

	colorBlending.blendConstants[0] = 0.0f;
	colorBlending.blendConstants[1] = 0.0f;
	colorBlending.blendConstants[2] = 0.0f;
	colorBlending.blendConstants[3] = 0.0f;
#pragma endregion

#pragma region Pipeline Layout
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	descriptorSetLayouts.reserve(desc.descriptorSets.size());
	for (const auto& descriptorSet : desc.descriptorSets)
	{
		auto* vkDescriptorSet = descriptorSet
			? dynamic_cast<VulkanDescriptorSet*>(descriptorSet.get())
			: nullptr;
		const VkDescriptorSetLayout setLayout = vkDescriptorSet
			? vkDescriptorSet->GetLayout()
			: VK_NULL_HANDLE;
		if (setLayout == VK_NULL_HANDLE)
		{
			Error_Core("VulkanPipeline: descriptor set has no valid Vulkan layout");
			return;
		}
		descriptorSetLayouts.push_back(setLayout);
	}

	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType =
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

	layoutInfo.setLayoutCount =
		static_cast<uint32_t>(descriptorSetLayouts.size());
	layoutInfo.pSetLayouts = descriptorSetLayouts.empty()
		? nullptr
		: descriptorSetLayouts.data();

	layoutInfo.pushConstantRangeCount = 0;
	layoutInfo.pPushConstantRanges = nullptr;

	auto& context = RHIContext::Get();
	if (!context)
	{
		Error_Core("VulkanPipeline: no active RHI context");
		return;
	}

	auto* vkcontext = dynamic_cast<RHIVulkanContext*>(context.get());
	if (!vkcontext || vkcontext->GetDevice() == VK_NULL_HANDLE)
	{
		Error_Core("VulkanPipeline: active RHI context is not a valid Vulkan context");
		return;
	}

	VkResult result = vkCreatePipelineLayout(
		vkcontext->GetDevice(),
		&layoutInfo,
		nullptr,
		&m_PipelineLayout);

	if (result != VK_SUCCESS)
	{
		Error_Core("Failed to create Vulkan pipeline layout");
		return;
	}
#pragma endregion


	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType =
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;

	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;

	pipelineInfo.layout = m_PipelineLayout;

	// Dynamic rendering derives the pipeline interface directly from RDG.
	if (!desc.renderingSignature.HasAttachments())
	{
		Error_Core("VulkanPipeline: graphics pipeline has no rendering signature");
		vkDestroyPipelineLayout(vkcontext->GetDevice(), m_PipelineLayout, nullptr);
		m_PipelineLayout = VK_NULL_HANDLE;
		return;
	}

	if (!vkcontext->SupportsDynamicRendering())
	{
		Error_Core("VulkanPipeline: dynamic rendering is not supported by the Vulkan context");
		vkDestroyPipelineLayout(vkcontext->GetDevice(), m_PipelineLayout, nullptr);
		m_PipelineLayout = VK_NULL_HANDLE;
		return;
	}

	std::vector<VkFormat> colorFormats;
	colorFormats.reserve(desc.renderingSignature.colorFormats.size());
	for (Format format : desc.renderingSignature.colorFormats)
	{
		const VkFormat vkFormat = VulkanPipelineUtil::ToVkFormat(format);
		if (vkFormat == VK_FORMAT_UNDEFINED)
		{
			vkDestroyPipelineLayout(vkcontext->GetDevice(), m_PipelineLayout, nullptr);
			m_PipelineLayout = VK_NULL_HANDLE;
			return;
		}
		colorFormats.push_back(vkFormat);
	}

	VkFormat depthFormat = VK_FORMAT_UNDEFINED;
	Format resolvedDepthFormat = Format::Unknown;
	if (desc.renderingSignature.depthFormat != Format::Unknown)
	{
		resolvedDepthFormat = VulkanPipelineUtil::ResolveDepthFormat(
			desc.renderingSignature.depthFormat);
		depthFormat = VulkanPipelineUtil::ToVkFormat(resolvedDepthFormat);
		if (depthFormat == VK_FORMAT_UNDEFINED)
		{
			vkDestroyPipelineLayout(vkcontext->GetDevice(), m_PipelineLayout, nullptr);
			m_PipelineLayout = VK_NULL_HANDLE;
			return;
		}
	}

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.viewMask = desc.renderingSignature.viewMask;
	renderingInfo.colorAttachmentCount =
		static_cast<uint32_t>(colorFormats.size());
	renderingInfo.pColorAttachmentFormats =
		colorFormats.empty() ? nullptr : colorFormats.data();
	renderingInfo.depthAttachmentFormat = depthFormat;
	renderingInfo.stencilAttachmentFormat =
		resolvedDepthFormat == Format::D24_UNORM_S8_UINT ||
		resolvedDepthFormat == Format::D32_SFLOAT_S8_UINT
			? renderingInfo.depthAttachmentFormat
			: VK_FORMAT_UNDEFINED;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.renderPass = VK_NULL_HANDLE;
	pipelineInfo.subpass = 0;

	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	if (vkCreateGraphicsPipelines(
		vkcontext->GetDevice(),
		VK_NULL_HANDLE,
		1,
		&pipelineInfo,
		nullptr,
		&m_Pipeline) != VK_SUCCESS)
	{
		Error_Core("Failed to create Vulkan graphics pipeline");

			vkDestroyPipelineLayout(
				vkcontext->GetDevice(),
				m_PipelineLayout,
				nullptr);

		m_PipelineLayout = VK_NULL_HANDLE;
		return;
	}
}

VulkanPipeline::~VulkanPipeline()
{
	auto& context = RHIContext::Get();
	if (!context)
		return;

	auto* vkcontext = dynamic_cast<RHIVulkanContext*>(context.get());
	if (!vkcontext || vkcontext->GetDevice() == VK_NULL_HANDLE)
		return;

	if (m_Pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(vkcontext->GetDevice(), m_Pipeline, nullptr);
	if (m_PipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(vkcontext->GetDevice(), m_PipelineLayout, nullptr);
}

void VulkanPipeline::Bind()
{
	auto& context = RHIContext::Get();
	if (!context)
		return;

	auto* vkcontext = dynamic_cast<RHIVulkanContext*>(context.get());
	if (vkcontext && m_Pipeline != VK_NULL_HANDLE &&
		vkcontext->GetCurrentVkCommandBuffer() != VK_NULL_HANDLE)
	{
		vkCmdBindPipeline(vkcontext->GetCurrentVkCommandBuffer(),
			VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
	}
}

void VulkanPipeline::Unbind()
{
}

