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
        float bloomThreshold = 0.8f, bloomKnee = 0.2f, bloomStrength = 0.35f, bloomRadius = 1.0f;
        float blurRadius = 1.0f;
        float focusDistance = 3.0f, focusRange = 0.65f, dofRadius = 8.0f;
        float motionStrength = 0.5f, motionMaxPixels = 16.0f;
        float exposure = 0.0f, gamma = 2.2f;
        float saturation = 1.15f;
        glm::vec3 colorTint{1.05f, 1.0f, 0.93f};
        float sharpenStrength = 0.25f, aberrationPixels = 1.5f;
        float vignetteStrength = 0.45f, vignetteInner = 0.35f, vignetteOuter = 1.3f;
        float fxaaThreshold = 0.125f, fxaaMinThreshold = 0.0312f, fxaaSpan = 8.0f;
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
			uint32_t width, uint32_t height, glm::vec4 controls = glm::vec4(0.0f))
			: m_Name(name), m_Shader(shader), m_Effect(effect), m_InputName(input),
			  m_AuxiliaryName(auxiliary), m_Parameters(parameters),
			  m_Width(width), m_Height(height), m_Controls(controls)
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
			desc.pushConstantSize = sizeof(glm::vec4) * 2;
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

			const glm::vec4 settings[] = { glm::vec4(m_Parameters, static_cast<float>(effect), 0.0f), m_Controls };
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
		glm::vec4 m_Controls;
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
		bool deferred, const glm::mat4& projection, const char* source = nullptr)
	{
		using Effect = FullscreenPass::Effect;
		const glm::vec2 horizontal(1.0f, 0.0f);
		const glm::vec2 vertical(0.0f, 1.0f);
		const glm::vec2 noParameters(0.0f);
		std::string color = source ? source : std::string();
		Ref<FullscreenPass> lastPass;
		const auto append = [&](const char* name, Effect effect, const char* auxiliary = nullptr,
			glm::vec2 parameters = glm::vec2(0.0f), glm::vec4 controls = glm::vec4(0.0f))
		{
			lastPass = CreateRef<FullscreenPass>(name, shader, effect, color, auxiliary,
				parameters, width, height, controls);
			graph.AddPass(lastPass);
			color = std::string(name) + ".Color";
		};

		if (settings.depthOfField)
			append("DepthOfField", Effect::DepthOfField,
				deferred ? "ForwardOpaque.Depth" : "Geometry.Depth",
				{projection[2][2], projection[3][2]},
                {settings.focusDistance, settings.focusRange, settings.dofRadius, 0.0f});
		if (settings.motionBlur)
			append("MotionBlur", Effect::MotionBlur,
				deferred ? "ForwardOpaque.Velocity" : "Geometry.Velocity", {},
                {settings.motionStrength, settings.motionMaxPixels, 0.0f, 0.0f});

		if (settings.bloom)
		{
			const uint32_t halfWidth = (width + 1) / 2;
			const uint32_t halfHeight = (height + 1) / 2;
			graph.AddPass(CreateRef<FullscreenPass>("BloomPrefilter", shader, Effect::Prefilter,
				color, nullptr, noParameters, halfWidth, halfHeight,
                glm::vec4(settings.bloomThreshold, settings.bloomKnee, 0.0f, 0.0f)));
			graph.AddPass(CreateRef<FullscreenPass>("BloomHorizontal", shader, Effect::Gaussian,
				"BloomPrefilter.Color", nullptr, horizontal * settings.bloomRadius, halfWidth, halfHeight));
			graph.AddPass(CreateRef<FullscreenPass>("BloomVertical", shader, Effect::Gaussian,
				"BloomHorizontal.Color", nullptr, vertical * settings.bloomRadius, halfWidth, halfHeight));
			append("Bloom", Effect::Composite, "BloomVertical.Color", {},
                {settings.bloomStrength, 0.0f, 0.0f, 0.0f});
		}

		if (settings.blur)
		{
			append("BlurHorizontal", Effect::Gaussian, nullptr, horizontal * settings.blurRadius);
			append("Blur", Effect::Gaussian, nullptr, vertical * settings.blurRadius);
		}

		if (settings.toneMapping)
			append("ToneMapping", Effect::ToneMapping, nullptr, {}, glm::vec4(settings.exposure, settings.gamma, 0.0f, 0.0f));
		if (settings.colorGrading)
			append("ColorGrading", Effect::ColorGrading, nullptr, {}, glm::vec4(settings.colorTint, settings.saturation));
		if (settings.sharpen)
			append("Sharpen", Effect::Sharpen, nullptr, {}, glm::vec4(settings.sharpenStrength, 0.0f, 0.0f, 0.0f));
		if (settings.chromaticAberration)
			append("ChromaticAberration", Effect::ChromaticAberration, nullptr, {}, glm::vec4(settings.aberrationPixels, 0.0f, 0.0f, 0.0f));
		if (settings.vignette)
			append("Vignette", Effect::Vignette, nullptr, {}, glm::vec4(settings.vignetteStrength, settings.vignetteInner, settings.vignetteOuter, 0.0f));
		if (settings.fxaa)
			append("FXAA", Effect::FXAA, nullptr, {}, glm::vec4(settings.fxaaThreshold, settings.fxaaMinThreshold, settings.fxaaSpan, 0.0f));

		if (lastPass)
			lastPass->ExportOutput();
		return color;
	}
}
