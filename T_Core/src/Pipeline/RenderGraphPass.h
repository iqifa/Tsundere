#pragma once
#include "Pipeline/RenderGraph.h"
#include "Platform/RHI/RHICommandBuffer.h"

// Forward declarations
class RenderGraphBuilder;
class RenderGraphContext;
class RenderGraphResources;

// ============================================================================
// RenderGraphPass - 统一的 Pass 基类
// ============================================================================
// 所有 Pass 继承这个基类，实现 Setup() 和 Execute() 方法。
// Setup 阶段声明资源依赖，Execute 阶段执行渲染命令。
class T_API RenderGraphPass
{
public:
	virtual ~RenderGraphPass() = default;

	// Pass 名称（用于调试和资源命名）
	virtual const char* GetName() const = 0;

	// Setup 阶段：声明资源依赖
	// 在 RenderGraph::Compile() 之前调用，用于构建依赖图
	virtual void Setup(RenderGraphBuilder& builder) = 0;

	// Execute 阶段：执行渲染命令
	// 在 RenderGraph::Execute() 时调用，资源已分配好
	virtual void Execute(RenderGraphContext& context) = 0;

	// 可选：动态启用/禁用 Pass
	virtual bool IsEnabled() const { return true; }
};

// ============================================================================
// RenderGraphBuilder - Setup 阶段的接口
// ============================================================================
// 在 Pass::Setup() 中使用，用于声明资源读写依赖。
class T_API RenderGraphBuilder
{
public:
	// ========================================
	// 资源读取（按名字）
	// ========================================
	// 读取已存在的纹理资源。如果资源不存在，返回 invalid handle。
	RGTextureHandle ReadTexture(const std::string& name, RGAccess access = RGAccess::ReadSRV);
	RGBufferHandle ReadBuffer(const std::string& name, RGAccess access = RGAccess::ReadSRV);

	// ========================================
	// 资源创建
	// ========================================
	// 创建新的纹理/缓冲资源。资源会自动命名为 "PassName.name"。
	RGTextureHandle CreateTexture(const std::string& name, const RDGTextureDesc& desc);
	RGBufferHandle CreateBuffer(const std::string& name, const RDGBufferDesc& desc);

	// ========================================
	// 导入外部资源
	// ========================================
	// 导入 Pass 外部管理的资源（例如 TAA 的 History）。
	RGTextureHandle ImportTexture(RHITexture2D* texture, const RDGTextureDesc& desc, const std::string& name);
	RGBufferHandle ImportBuffer(RHIBuffer* buffer, const RDGBufferDesc& desc, const std::string& name);

	// ========================================
	// 声明渲染目标
	// ========================================
	// 声明 Color/Depth 输出，内部会自动注册为 Write 依赖。
	void SetColorOutput(uint32_t slot, RGTextureHandle texture,
		RGLoadOp loadOp = RGLoadOp::Clear,
		const std::array<float, 4>& clearColor = { 0, 0, 0, 0 });

	void SetDepthOutput(RGTextureHandle texture,
		RGLoadOp loadOp = RGLoadOp::Clear,
		float clearDepth = 1.0f,
		bool readOnly = false);

	// ========================================
	// 资源导出
	// ========================================
	// 将资源标记为 exported，以便在 Graph 外部访问（例如显示到 ImGui）。
	void Export(RGTextureHandle texture);
	void Export(RGBufferHandle buffer);

	// ========================================
	// 辅助方法
	// ========================================
	// 获取当前 Pass 名称（用于资源命名）
	const std::string& GetPassName() const { return m_PassName; }

	// 为当前 Pass 创建 Pipeline（自动从 attachments 提取配置）
	Ref<RHIPipeline> CreatePipeline(
		Ref<RHIShader> shader,
		const VertexLayout& vertexLayout,
		const PipelineDesc* baseDesc = nullptr);

private:
	friend class RenderGraph;

	RenderGraphBuilder(RenderGraph& graph, RGPass& pass, const std::string& passName);

	RenderGraph& m_Graph;
	RGPass& m_Pass;
	std::string m_PassName;

	// 本地缓存：PassName.ResourceName -> Handle
	std::unordered_map<std::string, RGTextureHandle> m_LocalTextures;
	std::unordered_map<std::string, RGBufferHandle> m_LocalBuffers;
};

// ============================================================================
// RenderGraphContext - Execute 阶段的接口
// ============================================================================
// 在 Pass::Execute() 中使用，用于访问物理资源和提交渲染命令。
class T_API RenderGraphContext
{
public:
	// 获取命令缓冲
	RHICommandBuffer& GetCmd() const { return m_Cmd; }

	// 解析 Handle 到物理资源
	RHITexture2D* GetTexture(RGTextureHandle handle);
	RHIBuffer* GetBuffer(RGBufferHandle handle);

	// 便捷方法：绑定纹理到 Shader（自动处理 invalid handle）
	void BindTexture(uint32_t slot, RGTextureHandle handle);

private:
	friend class RenderGraph;

	RenderGraphContext(RHICommandBuffer& cmd, RenderGraphResources& resources)
		: m_Cmd(cmd), m_Resources(resources) {}

	RHICommandBuffer& m_Cmd;
	RenderGraphResources& m_Resources;
};
