#include<Pipeline/Passes/PassCommon.h>
#include<Pipeline/RenderPassRegistry.h>



class ShadowMapPass :public RenderGraphPass {
public:
	ShadowMapPass(Ref<Scene> scene, Ref<RGFrameData> frameData)
		: m_Scene(scene), m_FrameData(frameData) {


		m_LightViewProj = RHIBuffer::Create(BufferDesc{ sizeof(ViewUBO),BufferUsage::Uniform ,true,nullptr });
		m_FrameUBO = RHIBuffer::Create(BufferDesc{ sizeof(GbufferFrameUBO), BufferUsage::Uniform, true, nullptr });

		m_ShadowMapDepthShader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/ShadowMapDepth.shader");

		
		unsigned char black[4] = { 0, 0, 0, 0 };

		m_DefaultTex=RHITexture2D::Create({1,1,Format::RGBA8_UNORM,FilterMode::Linear,FilterMode::Linear,WrapMode::ClampToEdge,WrapMode::ClampToEdge,false,black});
		
		
		m_GbufferDescriptSet = RHIDescriptorSet::Create();

		m_ShadowMapDepthDescriptSet = RHIDescriptorSet::Create();
		if (m_ShadowMapDepthDescriptSet){
			m_ShadowMapDepthDescriptSet->BindUniformBuffer(0, m_LightViewProj);
			m_ShadowMapDepthDescriptSet->BindTexture(10, m_DefaultTex, 10);
		}
		else {
			Error_Core("[ShadowMapPass]:m_ShadowMapDepthDescriptSet is Null");
		}
	}
	const char* GetName() const override { return "ShadowMap"; }


	void Setup(RenderGraphBuilder& builder) override{
		RDGTextureDesc depthDesc;
		depthDesc.width = m_Width;
		depthDesc.height = m_Height;
		depthDesc.format = Format::D24_UNORM_S8_UINT;
		depthDesc.usage = TextureUsage::DepthStencil | TextureUsage::Sampled;

		m_ShadowMapDepth = builder.CreateTexture("ShadowMapDepth", depthDesc);

		builder.SetDepthOutput(m_ShadowMapDepth, RGLoadOp::Clear, 0.0f);

		if (!m_ScenePipeline)
		{
			VertexLayout sceneLayout;
			sceneLayout.stride = sizeof(Vertex);
			sceneLayout.attributes = {
				{ 0, VertexFormat::Float3, offsetof(Vertex, Position) },
				{ 1, VertexFormat::Float3, offsetof(Vertex, Normal) },
				{ 2, VertexFormat::Float2, offsetof(Vertex, TexCoords) },
				{ 3, VertexFormat::Float3, offsetof(Vertex, Tangent) },
				{ 4, VertexFormat::Float3, offsetof(Vertex, Bitangent) },
				{ 5, VertexFormat::Int4,   offsetof(Vertex, m_BoneIDs) },
				{ 6, VertexFormat::Float4, offsetof(Vertex, m_Weights) },
			};

			PipelineDesc scenePipeDesc = m_CubePipelineDesc;  // Reuse base settings
			scenePipeDesc.descriptorSets = { m_GbufferDescriptSet };
			scenePipeDesc.vertexLayout = sceneLayout;
			m_ScenePipeline = builder.CreatePipeline(m_GbufferShader, sceneLayout, &scenePipeDesc);
		}


		
	}

	struct ViewUBO {
		glm::mat4 LightviewProj;
	};


	Ref<Scene> m_Scene;
	Ref<RGFrameData> m_FrameData;
	Ref<RHIBuffer> m_LightViewProj;




	RGTextureHandle m_Depth;
	Ref<RHIBuffer> m_DrawUBO;
	Ref<RHIBuffer> m_FrameUBO;
	Ref<RHIDescriptorSet> m_ShdowPassDescriptSet;


	uint32_t m_Width = 1920;
	uint32_t m_Height = 1080;

	int m_FrameCount = 0;
	glm::mat4 m_PrevViewProjMatrix = glm::mat4(1.0f);
	Ref<RHIPipeline> m_ScenePipeline;  // Pipeline for scene meshes (7-attribute format)
	Ref<RHIShader> m_GbufferShader;



};