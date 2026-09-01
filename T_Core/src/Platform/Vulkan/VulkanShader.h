#pragma once
#include<ExternalFiles.h>
#include<HeadLine.h>
#include<Core/Core.h>
#include<Platform/RHI/RHIShader.h>
#include<Platform/RenderAPIConfig.h>
#include <utility>
#include <vulkan/vulkan.h>

class T_API VulkanShader :public RHIShader {
private:

	std::string m_Name;
	std::string m_FilePath;
	std::vector<Uniform> uniform;

	unsigned int m_HandleID;


public:
	VulkanShader(const std::string& filepath, const std::string& name);
	VulkanShader(const std::string& filepath);
	~VulkanShader();

	// Vulkan shader modules are consumed by VulkanPipeline; there is no
	// globally-bound shader program equivalent to an OpenGL program.
	void Bind() const override;
	void UnBind() const override;
	void DispatchCompute(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) const override;

	uint32_t GetID() const override { return m_HandleID; }
	const std::vector<Uniform>& GetUniforms() const override { return uniform; }

	void SetUniform4f(const std::string& name, float v0, float v1, float v2, float v3) const override;
	void SetUniform1f(const std::string& name, float value) const override;
	void SetUniform1i(const std::string& name, int value) const override;
	void SetUniformMat4f(const std::string& name, const glm::mat4& mat) const override;
	void SetUniformVec3(const std::string& name, const glm::vec3& value) const override;
	void SetUniformVec2(const std::string& name, const glm::vec2& value) const override;


	std::pair<VkShaderModule, VkShaderModule> GetGraphicsShaderModule();

	VkShaderModule GetComputeShaderModule();


	const std::string& GetName() const override { return m_Name; }
	const std::string& GetPath() const override { return m_FilePath; }

private:
	VkShaderModule CompileShader(unsigned int type, const std::string& source);
	//VkShaderModule CreateShader(const std::string& vertexShader, const std::string& fragmentShader);
	//VkShaderModule CreateComputeShader(const std::string& computeSource);


	VkShaderModule vertexShader = 0;
	VkShaderModule fragmentShader = 0;
	VkShaderModule computeShader = 0;
};