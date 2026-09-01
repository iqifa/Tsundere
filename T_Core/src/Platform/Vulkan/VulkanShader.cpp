#include "VulkanShader.h"
#include <shaderc/shaderc.h> // ���޸ĵ�1�������ô� C API ͷ�ļ�
#include <Platform/ShaderParser.h>
#include <Debug/Debug.h>
#include <Platform/RHI/RHIContext.h>
#include <Platform/RHI/RHIVulkanContext.h>

VulkanShader::VulkanShader(const std::string& filepath, const std::string& name) :m_FilePath(filepath), m_HandleID(0), m_Name(name)
{
	ParsedShader parsed = ParseShaderFile(filepath);

	uniform = std::move(parsed.uniforms);

	auto& sources = parsed.sources;
	auto itComp = sources.find(ShaderStage::Compute);
	if (itComp != sources.end())
	{
		computeShader = CompileShader(shaderc_glsl_compute_shader, itComp->second);
	}
	else {
		auto itVert = sources.find(ShaderStage::Vertex);
		auto itFrag = sources.find(ShaderStage::Fragment);

		std::string vertSrc = (itVert != sources.end()) ? itVert->second : "";
		std::string fragSrc = (itFrag != sources.end()) ? itFrag->second : "";

		if (!vertSrc.empty() || !fragSrc.empty())
		{
			vertexShader = CompileShader(shaderc_glsl_vertex_shader, vertSrc);
			fragmentShader = CompileShader(shaderc_glsl_fragment_shader, fragSrc);
		}
	}
	Info_Core("Successful Parse Shader:" + m_Name);
}

VulkanShader::VulkanShader(const std::string& filepath) :m_FilePath(filepath), m_HandleID(0)
{
	auto LastSlash = filepath.find_last_of("/");
	LastSlash = LastSlash == std::string::npos ? 0 : LastSlash + 1;
	auto LastDot = filepath.find_last_of(".");
	LastDot = LastSlash == std::string::npos ? filepath.length() : LastDot;
	auto count = LastDot - LastSlash;
	m_Name = filepath.substr(LastSlash, count);


	ParsedShader parsed = ParseShaderFile(filepath);

	uniform = std::move(parsed.uniforms);

	auto& sources = parsed.sources;
	auto itComp = sources.find(ShaderStage::Compute);
	if (itComp != sources.end())
	{
		computeShader = CompileShader(shaderc_glsl_compute_shader, itComp->second);
	}
	else {
		auto itVert = sources.find(ShaderStage::Vertex);
		auto itFrag = sources.find(ShaderStage::Fragment);

		std::string vertSrc = (itVert != sources.end()) ? itVert->second : "";
		std::string fragSrc = (itFrag != sources.end()) ? itFrag->second : "";

		if (!vertSrc.empty() || !fragSrc.empty())
		{
			vertexShader = CompileShader(shaderc_glsl_vertex_shader, vertSrc);
			fragmentShader = CompileShader(shaderc_glsl_fragment_shader, fragSrc);
		}
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

// ���޸ĵ�2�������ڲ�ʵ��ȫ���滻Ϊ C API
VkShaderModule VulkanShader::CompileShader(unsigned int type, const std::string& source)
{
	// 1. ��ʼ�� C �ӿڵı�����������
	shaderc_compiler_t compiler = shaderc_compiler_initialize();
	shaderc_compile_options_t options = shaderc_compile_options_initialize();

	auto& context = RHIContext::Get();
	auto* vkcontext = context ? dynamic_cast<RHIVulkanContext*>(context.get()) : nullptr;
	if (!vkcontext || vkcontext->GetDevice() == VK_NULL_HANDLE)
	{
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);
		Error_Core("Vulkan Shader[{0}] requires an active Vulkan context", m_Name);
		return VK_NULL_HANDLE;
	}

	shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

	// Release for Optimize
	// shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);

	// 2. ����Ϊ SPIR-V
	// ��ʾ��ԭ���봫���� source.c_str() ��Ϊ�ļ�������������Ὣ����Դ���ӡ���ļ����������Ż�Ϊ���� m_FilePath.c_str()
	shaderc_compilation_result_t result = shaderc_compile_into_spv(
		compiler,
		source.c_str(), source.length(),
		(shaderc_shader_kind)type,
		m_FilePath.c_str(),
		"main",
		options
	);

	// 3. ��鱨��
	if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
		Error_Core("Vulkan Shader[{0}] Compile Error:{1}", m_Name, shaderc_result_get_error_message(result));

		// �ͷ��ڴ沢����
		shaderc_result_release(result);
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);
		return 0;
	}

	// 4. ���� Shader Module
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	// ʹ�� C API ��ȡ���ݳ��ȣ��ֽ�������ָ��
	createInfo.codeSize = shaderc_result_get_length(result);
	createInfo.pCode = (const uint32_t*)shaderc_result_get_bytes(result);

	VkShaderModule shaderModuleHandle;
	bool createFailed = vkCreateShaderModule(vkcontext->GetDevice(), &createInfo, nullptr, &shaderModuleHandle) != VK_SUCCESS;

	// 5. �ص㣺�� Shaderc ������ڴ潻���� Shaderc �Ľӿ��ͷţ����׽�� CRT ��籨��
	shaderc_result_release(result);
	shaderc_compile_options_release(options);
	shaderc_compiler_release(compiler);

	if (createFailed)
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