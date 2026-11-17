#pragma once

// ---------------------------------------------------------------------------
// GPUPathTracePass（RDG 名称 "PathTrace"）—— OpenGL / Vulkan 共用的 GPU 路径追踪
//
// 一个 compute dispatch 完成整条路径：每个 invocation 一个像素，shader 内循环
// 样本与反弹；求交由后端适配层提供：
//   OpenGL                      软件两级 BVH（res/shaders/PathTraceBVH*.shader）
//   Vulkan + VK_KHR_ray_query   硬件 BLAS/TLAS（res/shaders/PathTraceRayQuery*.shader）
//   Vulkan 无 ray query         回退软件 BVH，并在 UI / 日志给出原因
// 积分器、材质、BSDF、采样、累积全部共享（include/PathTraceIntegrator.glsl）。
//
// 输出 "PathTrace.SceneColor"：持久化 RGBA32F 线性 HDR 累积纹理（导入到 RDG，
// 跨 graph 重建保留），存的是当前平均值。曝光 / 色调映射 / gamma 交给后处理
// （Post Processing → Tone Mapping），避免重复 gamma。
//
// 累积在以下情况清零：相机矩阵变化、几何/实例变换/材质/灯光变化（PathTraceScene）、
// 参数变化、尺寸变化（纹理重建）、shader 变体切换（天空盒有无 / 后端）、
// Pass 关闭后重开（实例重建）。
// ---------------------------------------------------------------------------

#include "Pipeline/Passes/PassCommon.h"
#include "Pipeline/PathTracing/PathTraceScene.h"
#include "Platform/RHI/RHIGpuTimer.h"
#include "Platform/RHI/RHIRayTracing.h"

#include <cstring>
#include <memory>
#include <string>

class GPUPathTracePass : public RenderGraphPass
{
public:
	struct Settings
	{
		int maxBounces = 4;
		int samplesPerFrame = 1;
		int maxSamples = 4096;            // 0 = 不停止
		int debugView = 0;                // PT_DEBUG_*
		int russianRouletteStart = 3;
		float fireflyClamp = 0.0f;        // 单样本亮度上限，0 = 关（无偏）
		glm::vec3 environmentColor{ 0.55f, 0.65f, 0.85f };
		float environmentIntensity = 1.0f;
		float debugDepthRange = 50.0f;
		bool paused = false;
		bool forceSoftwareBVH = false;    // Vulkan 上强制走软件 BVH（对照用）
		bool resetRequested = false;
	};

	struct Stats
	{
		bool active = false;
		std::string backend;
		std::string status;
		uint32_t samples = 0;
		uint32_t width = 0, height = 0;
		double dispatchGpuMs = -1.0;
		double graphGpuMs = -1.0;         // 由应用层填写：整个 RDG 的 GPU 时间
		float geometryCpuMs = 0.0f;       // 采集 + BVH 构建（CPU）+ 上传
		float bvhBuildCpuMs = 0.0f;
		float uploadCpuMs = 0.0f;
		RHIRayTracingBuildStats accel;
		uint64_t geometryRevision = 0;
		uint32_t tlasDepth = 0, blasDepth = 0;
	};

	// 单实例 Pass，设置 / 统计放在静态存储里供编辑器 UI 访问。
	static Settings& GetSettings() { static Settings s; return s; }
	static Stats& GetStats() { static Stats s; return s; }

	GPUPathTracePass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(std::move(scene)), m_FrameData(std::move(frameData))
	{
		m_FrameUBO = RHIBuffer::Create(BufferDesc{ sizeof(PathTracing::FrameUBO), BufferUsage::Uniform, true, nullptr });
		m_DispatchTimer = RHIGpuTimer::Create();
	}

	~GPUPathTracePass() override { GetStats().active = false; }

	const char* GetName() const override { return "PathTrace"; }

	void SetViewportSize(uint32_t width, uint32_t height) override
	{
		m_Width = width;
		m_Height = height;
	}

	void Setup(RenderGraphBuilder& builder) override
	{
		m_Output = {};
		if (m_Width == 0 || m_Height == 0)
			return;

		EnsureAccumulation();
		if (!EnsureVariant())
			return;

		RDGTextureDesc desc;
		desc.width = m_Width;
		desc.height = m_Height;
		desc.format = Format::RGBA32F;
		desc.usage = AccumUsage();
		desc.minFilter = desc.magFilter = FilterMode::Nearest;
		m_Output = builder.ImportTexture(m_Accum.get(), desc, "SceneColor");
		builder.WriteTexture(m_Output, RGAccess::WriteUAV);
		builder.Export(m_Output);
	}

	void Execute(RenderGraphContext& context) override
	{
		Stats& stats = GetStats();
		stats.active = true;
		if (m_Output.id == InvalidResourceId || !m_Pipeline || !currentcamera)
			return;

		Settings& settings = GetSettings();
		// UI 可能切换了“强制软件 BVH”；变体相同时这里什么都不做
		if (!EnsureVariant())
			return;
		bool reset = settings.resetRequested;
		settings.resetRequested = false;

		// ---- 场景数据（几何 / 材质 / 灯光）----
		if (m_PTScene->Update(*m_Scene))
			reset = true;
		if (m_PTScene->HasFailed() && m_Backend == PathTracing::Backend::RayQuery)
		{
			Warn_Core("[PathTrace] {}; falling back to the software BVH", m_PTScene->GetStatus());
			m_RayQueryFailed = true;
			if (!EnsureVariant())
				return;
			m_PTScene->Update(*m_Scene);
			reset = true;
		}
		if (!m_PTScene->IsReady())
		{
			stats.status = "scene not ready: " + m_PTScene->GetStatus();
			return;
		}

		// ---- 相机 / 参数变化 ----
		const glm::mat4 view = currentcamera->GetViewFront();
		const glm::mat4 proj = currentcamera->GetProj();
		if (std::memcmp(&view, &m_LastView, sizeof(view)) != 0 || std::memcmp(&proj, &m_LastProj, sizeof(proj)) != 0)
		{
			m_LastView = view;
			m_LastProj = proj;
			reset = true;
		}
		const void* sky = m_UseSkybox ? static_cast<const void*>(currentcamera->skybox->m_Cmp.get()) : nullptr;
		if (sky != m_LastSky)
		{
			m_LastSky = sky;
			reset = true;
		}
		const uint64_t settingsHash = HashSettings(settings);
		if (settingsHash != m_SettingsHash)
		{
			m_SettingsHash = settingsHash;
			reset = true;
		}
		if (reset)
			m_SampleCount = 0;

		UpdateStats();
		const uint32_t spp = static_cast<uint32_t>(std::max(settings.samplesPerFrame, 1));
		const bool converged = settings.maxSamples > 0 && m_SampleCount >= static_cast<uint32_t>(settings.maxSamples);
		if ((settings.paused && m_SampleCount > 0) || converged)
			return;   // 保留已累积结果，不再派发

		// ---- 每帧常量 ----
		PathTracing::FrameUBO ubo;
		ubo.invView = glm::inverse(view);
		ubo.invProj = glm::inverse(proj);
		ubo.cameraPos = glm::vec4(currentcamera->getpos(), 1.0f);
		ubo.frame = glm::uvec4(m_SampleCount, spp, static_cast<uint32_t>(std::max(settings.maxBounces, 0)), m_FrameSeed++);
		ubo.config = glm::uvec4(m_PTScene->GetLightCount(), static_cast<uint32_t>(settings.debugView), m_Width, m_Height);
		ubo.environment = glm::vec4(settings.environmentColor, settings.environmentIntensity);
		ubo.params = glm::vec4(settings.fireflyClamp, static_cast<float>(settings.russianRouletteStart),
			settings.debugDepthRange, 0.0f);
		m_FrameUBO->Upload(&ubo, sizeof(ubo));

		// ---- 绑定 ----
		RHITexture2D* accum = context.GetTexture(m_Output);
		BVHBuilder& geometry = m_PTScene->GetGeometry();
		m_Descriptors->Reset();
		m_Descriptors->BindUniformBuffer(0, m_FrameUBO);
		m_Descriptors->BindStorageTexture(1, accum, ImageAccess::ReadWrite, 1);
		m_Descriptors->BindStorageBuffer(2, m_PTScene->GetMaterialBuffer());
		m_Descriptors->BindStorageBuffer(3, m_PTScene->GetLightBuffer());
		if (m_UseSkybox)
			m_Descriptors->BindCubeMap(4, currentcamera->skybox->m_Cmp, 4);
		m_Descriptors->BindStorageBuffer(9, geometry.GetInstanceBuffer());
		m_Descriptors->BindStorageBuffer(11, geometry.GetTriangleBuffer());
		m_Descriptors->BindStorageBuffer(12, geometry.GetTriangleAttribBuffer());
		if (m_Backend == PathTracing::Backend::SoftwareBVH)
		{
			m_Descriptors->BindStorageBuffer(8, geometry.GetTLASNodeBuffer());
			m_Descriptors->BindStorageBuffer(10, geometry.GetBLASNodeBuffer());
		}
		else
		{
			m_Descriptors->BindAccelerationStructure(13, m_PTScene->GetRayTracingScene());
		}
		m_Descriptors->Apply(0);

		// ---- 派发 ----
		auto& cmd = context.GetCmd();
		// 上一帧本 pass 的 imageStore → 本帧 imageLoad（RDG 只看得见同一次执行内的依赖）
		cmd.ResourceBarrier(BarrierFlags::ShaderImage | BarrierFlags::StorageBuffer);
		cmd.BindPipeline(m_Pipeline);
		cmd.BindDescriptorSet(m_Descriptors, 0);
		if (m_DispatchTimer)
			m_DispatchTimer->Begin(cmd);
		cmd.Dispatch((m_Width + 7) / 8, (m_Height + 7) / 8, 1);
		if (m_DispatchTimer)
			m_DispatchTimer->End(cmd);
		m_Pipeline->Unbind();

		m_SampleCount += spp;
		stats.samples = m_SampleCount;
	}

private:
	static TextureUsage AccumUsage()
	{
		return TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::TransferSrc;
	}

	void EnsureAccumulation()
	{
		if (m_Accum && m_Accum->GetWidth() == m_Width && m_Accum->GetHeight() == m_Height)
			return;
		Texture2DDesc desc;
		desc.width = m_Width;
		desc.height = m_Height;
		desc.format = Format::RGBA32F;
		// RGBA32F 的线性过滤不是 Vulkan 必选特性；显示链路逐像素读取即可
		desc.minFilter = desc.magFilter = FilterMode::Nearest;
		desc.usage = AccumUsage();
		m_Accum = RHITexture2D::Create(desc);
		m_SampleCount = 0;
	}

	// 选后端 + 天空盒变体；变化时重建 shader / 描述符布局 / pipeline，并清空累积。
	bool EnsureVariant()
	{
		using PathTracing::Backend;
		const bool rayQuery = RHIRayTracingScene::IsSupported() && !GetSettings().forceSoftwareBVH && !m_RayQueryFailed;
		const Backend backend = rayQuery ? Backend::RayQuery : Backend::SoftwareBVH;
		const bool skybox = currentcamera && currentcamera->skybox && currentcamera->skybox->m_Cmp;
		const char* path = backend == Backend::RayQuery
			? (skybox ? "res/shaders/PathTraceRayQuerySky.shader" : "res/shaders/PathTraceRayQuery.shader")
			: (skybox ? "res/shaders/PathTraceBVHSky.shader" : "res/shaders/PathTraceBVH.shader");

		if (!m_PTScene || backend != m_Backend)
		{
			m_PTScene = std::make_unique<PathTraceScene>(backend);
			m_Backend = backend;
			m_SampleCount = 0;
			if (backend == Backend::SoftwareBVH && RHIRayTracingScene::IsSupported() == false)
				m_BackendNote = RHIRayTracingScene::GetUnsupportedReason();
			else if (backend == Backend::SoftwareBVH && m_RayQueryFailed)
				m_BackendNote = "ray query scene build failed; software BVH fallback";
			else
				m_BackendNote.clear();
		}

		if (m_Pipeline && m_ShaderPath == path && m_UseSkybox == skybox)
			return true;

		m_ShaderPath = path;
		m_UseSkybox = skybox;
		m_SampleCount = 0;
		m_Pipeline.reset();
		m_Shader = RHIShader::CreateCompute(path);
		if (!m_Shader || !ValidateShader())
		{
			Error_Core("[PathTrace] shader {} failed to build; pass disabled", path);
			m_Shader.reset();
			return false;
		}

		// 描述符布局在第一次 Apply 前冻结：这里声明本变体用到的全部 binding。
		m_Descriptors = RHIDescriptorSet::Create();
		m_Descriptors->BindUniformBuffer(0, m_FrameUBO);
		m_Descriptors->BindStorageTexture(1, nullptr, ImageAccess::ReadWrite, 1);
		m_Descriptors->BindStorageBuffer(2, nullptr);
		m_Descriptors->BindStorageBuffer(3, nullptr);
		if (skybox)
			m_Descriptors->BindCubeMap(4, nullptr, 4);
		m_Descriptors->BindStorageBuffer(9, nullptr);
		m_Descriptors->BindStorageBuffer(11, nullptr);
		m_Descriptors->BindStorageBuffer(12, nullptr);
		if (backend == Backend::SoftwareBVH)
		{
			m_Descriptors->BindStorageBuffer(8, nullptr);
			m_Descriptors->BindStorageBuffer(10, nullptr);
		}
		else
		{
			m_Descriptors->BindAccelerationStructure(13, nullptr);
		}

		PipelineDesc desc;
		desc.shader = m_Shader;
		desc.isCompute = true;
		desc.descriptorSets = { m_Descriptors };
		m_Pipeline = RHIPipeline::Create(desc);
		if (!m_Pipeline)
			Error_Core("[PathTrace] failed to create compute pipeline for {}", path);
		return m_Pipeline != nullptr;
	}

	// 宿主结构与着色器块的布局必须一致（反射校验，不一致直接拒绝运行）。
	bool ValidateShader() const
	{
		const ShaderReflection& r = m_Shader->GetReflection();
		bool ok = true;
		if (const ShaderBlock* ubo = r.FindUniformBlock(0); ubo && ubo->Size != sizeof(PathTracing::FrameUBO))
		{
			Error_Core("[PathTrace] FrameUBO is {} bytes, shader block {} is {}", sizeof(PathTracing::FrameUBO), ubo->Name, ubo->Size);
			ok = false;
		}
		const auto checkStride = [&](uint32_t binding, uint32_t stride, const char* what)
		{
			const ShaderBlock* block = r.FindStorageBlock(binding);
			if (block && !block->Members.empty() && block->Members[0].ArrayStride != 0 &&
				block->Members[0].ArrayStride != stride)
			{
				Error_Core("[PathTrace] {} stride is {} bytes, shader uses {}", what, stride, block->Members[0].ArrayStride);
				ok = false;
			}
		};
		checkStride(2, sizeof(PathTracing::GPUMaterial), "GPUMaterial");
		checkStride(3, sizeof(PathTracing::GPULight), "GPULight");
		checkStride(8, sizeof(GPUBVHNode), "GPUBVHNode");
		checkStride(9, sizeof(GPUBVHInstance), "GPUBVHInstance");
		checkStride(11, sizeof(GPUBVHTriangle), "GPUBVHTriangle");
		checkStride(12, sizeof(GPUBVHTriangleAttrib), "GPUBVHTriangleAttrib");
		return ok;
	}

	static uint64_t HashSettings(const Settings& s)
	{
		uint64_t h = 1469598103934665603ull;
		const auto mix = [&h](const void* data, size_t size)
		{
			const auto* p = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
		};
		// 只有影响积分结果的参数参与；maxSamples / paused 只决定是否继续派发
		mix(&s.maxBounces, sizeof(s.maxBounces));
		mix(&s.debugView, sizeof(s.debugView));
		mix(&s.russianRouletteStart, sizeof(s.russianRouletteStart));
		mix(&s.fireflyClamp, sizeof(s.fireflyClamp));
		mix(&s.environmentColor, sizeof(s.environmentColor));
		mix(&s.environmentIntensity, sizeof(s.environmentIntensity));
		mix(&s.debugDepthRange, sizeof(s.debugDepthRange));
		return h;
	}

	void UpdateStats()
	{
		Stats& stats = GetStats();
		const BVHBuilder& geometry = m_PTScene->GetGeometry();
		stats.backend = m_Backend == PathTracing::Backend::RayQuery
			? "Vulkan Ray Query (hardware BLAS/TLAS)"
			: (RHIRayTracingScene::IsSupported() ? "Software BVH (forced)" : "Software BVH");
		stats.status = m_PTScene->GetStatus();
		if (!m_BackendNote.empty())
			stats.status += "\n" + m_BackendNote;
		stats.width = m_Width;
		stats.height = m_Height;
		stats.samples = m_SampleCount;
		stats.dispatchGpuMs = m_DispatchTimer ? m_DispatchTimer->GetMilliseconds() : -1.0;
		stats.geometryCpuMs = m_PTScene->GetLastGatherMilliseconds();
		stats.bvhBuildCpuMs = geometry.GetStats().buildMilliseconds;
		stats.uploadCpuMs = geometry.GetUploadMilliseconds();
		stats.geometryRevision = m_PTScene->GetGeometryRevision();
		stats.tlasDepth = geometry.GetStats().tlasDepth;
		stats.blasDepth = geometry.GetStats().blasDepth;
		if (auto rt = m_PTScene->GetRayTracingScene())
			stats.accel = rt->GetStats();
		else
			stats.accel = RHIRayTracingBuildStats{};
	}

	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	std::unique_ptr<PathTraceScene> m_PTScene;
	PathTracing::Backend m_Backend = PathTracing::Backend::SoftwareBVH;
	bool m_RayQueryFailed = false;
	std::string m_BackendNote;

	Ref<RHIShader> m_Shader;
	std::string m_ShaderPath;
	bool m_UseSkybox = false;
	Ref<RHIDescriptorSet> m_Descriptors;
	Ref<RHIPipeline> m_Pipeline;
	Ref<RHIBuffer> m_FrameUBO;
	Ref<RHIGpuTimer> m_DispatchTimer;

	Ref<RHITexture2D> m_Accum;
	RGTextureHandle m_Output;
	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;
	uint32_t m_SampleCount = 0;
	uint32_t m_FrameSeed = 0;
	uint64_t m_SettingsHash = 0;
	glm::mat4 m_LastView{ 0.0f };
	glm::mat4 m_LastProj{ 0.0f };
	const void* m_LastSky = nullptr;
};

REGISTER_RENDER_PASS(GPUPathTracePass, "PathTrace", "RayTracing", 5, false)
