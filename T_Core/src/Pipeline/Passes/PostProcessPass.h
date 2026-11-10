#pragma once

#include "Pipeline/RenderGraphPass.h"

namespace PostProcess
{
	inline constexpr const char* SceneColor = "DeferredLight.Light";
	inline constexpr const char* FallbackSceneColor = "Geometry.SceneColor";
	struct Settings
	{
		bool bloom = true;
		bool blur = false;
		bool depthOfField = false;
		bool motionBlur = false;
		bool toneMapping = false;
		bool colorGrading = false;
		bool sharpen = false;
		bool chromaticAberration = false;
		bool vignette = false;
		bool fxaa = false;
	};

	class FullscreenPass final : public RenderGraphPass
	{
	public:
		// Values shared with PostProcess.shader.
		enum class Effect
		{
			Prefilter, Gaussian, Composite, DepthOfField, MotionBlur,
			ToneMapping, ColorGrading, Sharpen, ChromaticAberration, Vignette, FXAA, Copy
		};

		FullscreenPass(const char* name, Ref<RHIShader> shader, Effect effect,
			std::string input, const char* auxiliary, glm::vec2 parameters,
			uint32_t width, uint32_t height)
			: m_Name(name), m_Shader(shader), m_Effect(effect), m_InputName(input),
			  m_AuxiliaryName(auxiliary), m_Parameters(parameters),
			  m_Width(width), m_Height(height)
		{
		}

		const char* GetName() const override { return m_Name; }
		void ExportOutput() { m_Exported = true; }

		void Setup(RenderGraphBuilder& builder) override
		{
			m_Input = builder.ReadTexture(m_InputName.empty() ? SceneColor : m_InputName);
			if (m_Input.id == InvalidResourceId && m_InputName.empty())
				m_Input = builder.ReadTexture(FallbackSceneColor);
			if (m_Input.id == InvalidResourceId)
				return;

			if (m_AuxiliaryName)
				m_Auxiliary = builder.ReadTexture(m_AuxiliaryName);

			m_Descriptors = RHIDescriptorSet::Create();
			// Declare the sampler bindings before creating the pipeline layout.
			m_Descriptors->BindTexture(0, static_cast<RHITexture2D*>(nullptr), 0);
			m_Descriptors->BindTexture(1, static_cast<RHITexture2D*>(nullptr), 1);

			RDGTextureDesc target;
			target.width = m_Width;
			target.height = m_Height;
			target.format = Format::RGBA16F;
			target.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
			auto output = builder.CreateTexture("Color", target);
			builder.SetColorOutput(0, output, RGLoadOp::DontCare);

			PipelineDesc desc;
			desc.depthTest = false;
			desc.depthWrite = false;
			desc.cullMode = CullMode::None;
			desc.pushConstantSize = sizeof(glm::vec4);
			desc.descriptorSets = { m_Descriptors };
			m_Pipeline = builder.CreatePipeline(m_Shader, {}, &desc);

			if (m_Exported && m_Pipeline)
				builder.Export(output);
		}

		void Execute(RenderGraphContext& context) override
		{
			if (!m_Pipeline)
				return;

			auto* input = context.GetTexture(m_Input);
			if (!input)
				return;

			auto* auxiliary = m_Auxiliary.id != InvalidResourceId ? context.GetTexture(m_Auxiliary) : nullptr;
			const Effect effect = m_AuxiliaryName && !auxiliary ? Effect::Copy : m_Effect;
			m_Descriptors->Reset();
			m_Descriptors->BindTexture(0, input, 0);
			m_Descriptors->BindTexture(1, auxiliary ? auxiliary : input, 1);
			m_Descriptors->Apply(0);

			const glm::vec4 settings(m_Parameters, static_cast<float>(effect), 0.0f);
			auto& cmd = context.GetCmd();
			cmd.BindPipeline(m_Pipeline);
			cmd.BindDescriptorSet(m_Descriptors, 0);
			cmd.PushConstants(&settings, sizeof(settings));
			cmd.Draw(3);
		}

	private:
		const char* m_Name;
		Ref<RHIShader> m_Shader;
		Effect m_Effect;
		std::string m_InputName;
		const char* m_AuxiliaryName;
		glm::vec2 m_Parameters;
		uint32_t m_Width;
		uint32_t m_Height;
		bool m_Exported = false;
		RGTextureHandle m_Input;
		RGTextureHandle m_Auxiliary;
		Ref<RHIDescriptorSet> m_Descriptors;
		Ref<RHIPipeline> m_Pipeline;
	};

	inline std::string AddPasses(RenderGraph& graph, const Ref<RHIShader>& shader,
		uint32_t width, uint32_t height, const Settings& settings,
		bool deferred, const glm::mat4& projection)
	{
		using Effect = FullscreenPass::Effect;
		const glm::vec2 horizontal(1.0f, 0.0f);
		const glm::vec2 vertical(0.0f, 1.0f);
		const glm::vec2 noParameters(0.0f);
		std::string color;
		Ref<FullscreenPass> lastPass;
		const auto append = [&](const char* name, Effect effect, const char* auxiliary = nullptr,
			glm::vec2 parameters = glm::vec2(0.0f))
		{
			lastPass = CreateRef<FullscreenPass>(name, shader, effect, color, auxiliary,
				parameters, width, height);
			graph.AddPass(lastPass);
			color = std::string(name) + ".Color";
		};

		if (settings.depthOfField)
			append("DepthOfField", Effect::DepthOfField,
				deferred ? "Gbuffer.Depth" : "Geometry.Depth",
				{projection[2][2], projection[3][2]});
		if (settings.motionBlur)
			append("MotionBlur", Effect::MotionBlur,
				deferred ? "Gbuffer.Velocity" : "Geometry.Velocity");

		if (settings.bloom)
		{
			const uint32_t halfWidth = (width + 1) / 2;
			const uint32_t halfHeight = (height + 1) / 2;
			graph.AddPass(CreateRef<FullscreenPass>("BloomPrefilter", shader, Effect::Prefilter,
				color, nullptr, noParameters, halfWidth, halfHeight));
			graph.AddPass(CreateRef<FullscreenPass>("BloomHorizontal", shader, Effect::Gaussian,
				"BloomPrefilter.Color", nullptr, horizontal, halfWidth, halfHeight));
			graph.AddPass(CreateRef<FullscreenPass>("BloomVertical", shader, Effect::Gaussian,
				"BloomHorizontal.Color", nullptr, vertical, halfWidth, halfHeight));
			append("Bloom", Effect::Composite, "BloomVertical.Color");
		}

		if (settings.blur)
		{
			append("BlurHorizontal", Effect::Gaussian, nullptr, horizontal);
			append("Blur", Effect::Gaussian, nullptr, vertical);
		}

		if (settings.toneMapping)
			append("ToneMapping", Effect::ToneMapping);
		if (settings.colorGrading)
			append("ColorGrading", Effect::ColorGrading);
		if (settings.sharpen)
			append("Sharpen", Effect::Sharpen);
		if (settings.chromaticAberration)
			append("ChromaticAberration", Effect::ChromaticAberration);
		if (settings.vignette)
			append("Vignette", Effect::Vignette);
		if (settings.fxaa)
			append("FXAA", Effect::FXAA);

		if (lastPass)
			lastPass->ExportOutput();
		return color;
	}
}
