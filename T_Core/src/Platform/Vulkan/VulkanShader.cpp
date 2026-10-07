#include "VulkanShader.h"
#include <Platform/ShaderCompiler.h>
#include <Debug/Debug.h>
#include <Platform/RHI/RHIContext.h>
#include <Platform/RHI/RHIVulkanContext.h>

VulkanShader::VulkanShader(const std::string& filepath, const std::string& name) :m_FilePath(filepath), m_HandleID(0), m_Name(name)
{
	Build(filepath);
}

VulkanShader::VulkanShader(const std::string& filepath) :m_FilePath(filepath), m_HandleID(0)
{
	auto LastSlash = filepath.find_last_of("/");
	LastSlash = LastSlash == std::string::npos ? 0 : LastSlash + 1;
	auto LastDot = filepath.find_last_of(".");
	LastDot = LastSlash == std::string::npos ? filepath.length() : LastDot;
	auto count = LastDot - LastSlash;
	m_Name = filepath.substr(LastSlash, count);

	Build(filepath);
}

void VulkanShader::Build(const std::string& filepath)
{
	CompiledShader compiled = CompileShaderFile(filepath, ShaderTarget::Vulkan);
	if (!compiled.Success)
	{
		Error_Core("Vulkan Shader[{0}] build failed", m_Name);
		return;
	}

	uniform = std::move(compiled.Uniforms);
	m_Reflection = std::move(compiled.Reflection);

	auto& spirv = compiled.Spirv;
	if (auto it = spirv.find(ShaderStage::Compute); it != spirv.end())
	{
		computeShader = CreateShaderModule(it->second);
	}
	else
	{
		if (auto it = spirv.find(ShaderStage::Vertex); it != spirv.end())
			vertexShader = CreateShaderModule(it->second);
		if (auto it = spirv.find(ShaderStage::Fragment); it != spirv.end())
			fragmentShader = CreateShaderModule(it->second);
	}
	Info_Core("Successful Parse Shader:" + m_Name);
}

VulkanShader::~VulkanShader()
{
}

void VulkanShader::Bind() const
{
	// Vulkan shaders are bound as part of a graphics/compute pipeline.
}

void VulkanShader::UnBind() const
{
	// There is no global Vulkan shader-program binding to clear.
}

void VulkanShader::DispatchCompute(uint32_t, uint32_t, uint32_t) const
{
	// Dispatch is recorded by RHICommandBuffer after pipeline binding.
}

void VulkanShader::SetUniform4f(const std::string&, float, float, float, float) const
{
	// Pending descriptor-set/UBO implementation for the Vulkan backend.
}

void VulkanShader::SetUniform1f(const std::string&, float) const
{
	// Pending descriptor-set/UBO implementation for the Vulkan backend.
}

void VulkanShader::SetUniform1i(const std::string&, int) const
{
	// Pending descriptor-set/UBO implementation for the Vulkan backend.
}

void VulkanShader::SetUniformMat4f(const std::string&, const glm::mat4&) const
{
	// Pending descriptor-set/UBO implementation for the Vulkan backend.
}

void VulkanShader::SetUniformVec3(const std::string&, const glm::vec3&) const
{
	// Pending descriptor-set/UBO implementation for the Vulkan backend.
}

void VulkanShader::SetUniformVec2(const std::string&, const glm::vec2&) const
{
	// Pending descriptor-set/UBO implementation for the Vulkan backend.
}

std::pair<VkShaderModule, VkShaderModule> VulkanShader::GetGraphicsShaderModule()
{
	return { vertexShader,fragmentShader };
}

VkShaderModule VulkanShader::GetComputeShaderModule()
{
	return computeShader;
}

VkShaderModule VulkanShader::CreateShaderModule(const std::vector<uint32_t>& spirv)
{
	auto& context = RHIContext::Get();
	auto* vkcontext = context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
	if (!vkcontext || vkcontext->GetDevice() == VK_NULL_HANDLE)
	{
		Error_Core("Vulkan Shader[{0}] requires an active Vulkan context", m_Name);
		return VK_NULL_HANDLE;
	}

	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = spirv.size() * sizeof(uint32_t);
	createInfo.pCode = spirv.data();

	VkShaderModule shaderModuleHandle = VK_NULL_HANDLE;
	if (vkCreateShaderModule(vkcontext->GetDevice(), &createInfo, nullptr, &shaderModuleHandle) != VK_SUCCESS)
	{
		Error_Core("Vulkan Shader[{0}] Create Shader Moudle Error", m_Name);
		return VK_NULL_HANDLE;
	}

	return shaderModuleHandle;
}

#ifdef RenderAPI_Vulkan
std::shared_mutex ShaderLibiray::s_Mutex;
std::unordered_map<std::string, Ref<RHIShader>> ShaderLibiray::m_Shaders;


Ref<RHIShader> RHIShader::Create(const std::string& filepath)
{
	return CreateRef<VulkanShader>(filepath);
}

Ref<RHIShader> RHIShader::Create(
	const std::string& filepath,
	const std::string& name)
{
	return CreateRef<VulkanShader>(filepath, name);
}

Ref<RHIShader> RHIShader::CreateCompute(const std::string& filepath)
{
	return CreateRef<VulkanShader>(filepath);
}

#endif // RenderAPI_Vulkan
