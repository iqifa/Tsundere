#pragma once
#include "Core/Layer/Layer.h"
#include "ExternalFiles.h"
#include "Panels/MeshFilePath.h"
#include "Core/Threading/ResourceLoader.h"
#include "Scene/BVHBuilder.h"
#include "Scene/Scene.h"
#include "Scene/Vertex.h"
#include "HeadLine.h"
#include "Core/Application.h"
#include<Platform/RenderAPI.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// Mesh list, preview camera, and bone names for one loaded model.
// Call BindScene once, then Open() creates the layer the first time it is shown.
class ModelEditorPanel : public Engine::Layer
{
public:
	static void BindScene(Ref<Scene> scene)
	{
		s_Scene = std::move(scene);
	}

	ModelEditorPanel(Ref<Scene> scene, const std::string& name = "Model Editor")
		: Layer(name), m_Scene(std::move(scene))
	{
		s_Instance = this;
	}

	~ModelEditorPanel() override
	{
		if (s_Instance == this)
			s_Instance = nullptr;
		if (m_PreviewFramebuffer)
		{
			if (RHITexture2D* color = m_PreviewFramebuffer->GetColorAttachment(0))
				RHIImGUIRenderer::ReleaseTexture(color);
		}
	}

	static void Open(const std::string& path)
	{
		if (path.empty() || !s_Scene)
			return;
		if (!s_Instance)
			Engine::Application::Get().PushLayer(new ModelEditorPanel(s_Scene, "Model Editor"));
		if (s_Instance)
			s_Instance->OpenPath(path);
	}

	static void CloseIfShowing(const std::string& path)
	{
		if (s_Instance && s_Instance->m_Open && s_Instance->m_SelectedPath == path)
			s_Instance->m_Open = false;
	}

	static const std::string& ShowingPath()
	{
		static const std::string empty;
		if (!s_Instance || !s_Instance->m_Open)
			return empty;
		return s_Instance->m_SelectedPath;
	}

	void OnImGuiRender() override
	{
		if (!m_Open)
		{
			// BeginFrame has already waited for the frame that recorded the
			// preview, so the pipeline, descriptor set, and image can be freed.
			DestroySelf();
			return;
		}
		DrawEditorWindow();
	}

private:
	void OpenPath(const std::string& path)
	{
		if (path.empty())
			return;
		if (path != m_SelectedPath)
		{
			m_SelectedMesh = 0;
			m_BoneFilter[0] = 0;
			m_MeshColors.clear();
		}
		m_SelectedPath = path;
		m_Open = true;
	}

	void DrawEditorWindow()
	{
		if (!m_Open || m_SelectedPath.empty())
			return;

		ImGui::SetNextWindowSize(ImVec2(680.0f, 900.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Model Editor", &m_Open))
		{
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted(FileName(m_SelectedPath).c_str());
		ImGui::TextWrapped("%s", m_SelectedPath.c_str());

		Ref<Model> model = My_map::GetModel(m_SelectedPath);
		if (!model)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Loading...");
			ImGui::End();
			return;
		}

		if (m_SelectedMesh >= (int)model->meshes.size())
			m_SelectedMesh = 0;

		size_t vertexCount = 0;
		size_t indexCount = 0;
		for (const auto& mesh : model->meshes)
		{
			vertexCount += mesh.vertices.size();
			indexCount += mesh.indices.size();
		}

		ImGui::Separator();
		ImGui::Text("Meshes %d    Vertices %zu    Triangles %zu    Bones %zu",
			(int)model->meshes.size(), vertexCount, indexCount / 3, model->bones.size());

		glm::vec3 boundsMin(0.0f);
		glm::vec3 boundsMax(0.0f);
		if (ComputeBounds(*model, boundsMin, boundsMax))
		{
			ImGui::Text("Bounds min  %.3f  %.3f  %.3f", boundsMin.x, boundsMin.y, boundsMin.z);
			ImGui::Text("Bounds max  %.3f  %.3f  %.3f", boundsMax.x, boundsMax.y, boundsMax.z);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Meshes");
		EnsureMeshColors(*model);
		if (ImGui::BeginTable("##meshes", 5,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
			ImVec2(0, 160)))
		{
			ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthFixed, 50.0f);
			ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 36.0f);
			ImGui::TableSetupColumn("Vertices");
			ImGui::TableSetupColumn("Triangles");
			ImGui::TableSetupColumn("GPU");
			ImGui::TableHeadersRow();
			for (int i = 0; i < (int)model->meshes.size(); i++)
			{
				const auto& mesh = model->meshes[i];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				char label[32];
				std::snprintf(label, sizeof(label), "%d", i);
				if (ImGui::Selectable(label, m_SelectedMesh == i, ImGuiSelectableFlags_AllowDoubleClick))
				{
					m_SelectedMesh = i;
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						FocusPreviewOnMesh(mesh);
				}
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::PushID(i);
				ImGui::ColorEdit3("##meshcolor", &m_MeshColors[i].x, ImGuiColorEditFlags_NoInputs);
				ImGui::PopID();
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%zu", mesh.vertices.size());
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%zu", mesh.indices.size() / 3);
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(mesh.IsGPUReady() ? "ready" : "pending");
			}
			ImGui::EndTable();
		}

		if (!model->meshes.empty())
		{
			const auto& mesh = model->meshes[m_SelectedMesh];
			int skinned = 0;
			for (const auto& v : mesh.vertices)
			{
				if (v.m_Weights[0] != 0.0f)
					skinned++;
			}
			ImGui::Text("Mesh %d    skinned vertices %d / %zu",
				m_SelectedMesh, skinned, mesh.vertices.size());
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete mesh"))
				DeleteSelectedMesh(*model);
		}

		ImGui::Separator();
		DrawModelPreview(*model, boundsMin, boundsMax);

		ImGui::Separator();
		if (ImGui::TreeNodeEx("Bones", ImGuiTreeNodeFlags_DefaultOpen, "Bones (%zu)", model->bones.size()))
		{
			ImGui::InputTextWithHint("##bonefilter", "filter bone name", m_BoneFilter, sizeof(m_BoneFilter));
			if (ImGui::BeginTable("##bones", 2,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
				ImVec2(0, 220)))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.0f);
				ImGui::TableSetupColumn("Name");
				ImGui::TableHeadersRow();
				for (int i = 0; i < (int)model->bones.size(); i++)
				{
					auto& bone = model->bones[i];
					if (m_BoneFilter[0] != 0 && bone.name.find(m_BoneFilter) == std::string::npos)
						continue;

					ImGui::PushID(i);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%d", i);
					ImGui::TableSetColumnIndex(1);
					char nameBuf[128];
					std::snprintf(nameBuf, sizeof(nameBuf), "%s", bone.name.c_str());
					ImGui::SetNextItemWidth(-FLT_MIN);
					if (ImGui::InputText("##bonename", nameBuf, sizeof(nameBuf)))
						bone.name = nameBuf;
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}

		ImGui::End();
	}

	static std::string FileName(const std::string& path)
	{
		return path.substr(path.find_last_of("/\\") + 1);
	}

	static bool ComputeBounds(const Model& model, glm::vec3& outMin, glm::vec3& outMax)
	{
		bool any = false;
		glm::vec3 bmin(FLT_MAX);
		glm::vec3 bmax(-FLT_MAX);
		for (const auto& mesh : model.meshes)
		{
			for (const auto& v : mesh.vertices)
			{
				bmin = glm::min(bmin, v.Position);
				bmax = glm::max(bmax, v.Position);
				any = true;
			}
		}
		if (!any)
			return false;
		outMin = bmin;
		outMax = bmax;
		return true;
	}

	struct PreviewPush
	{
		glm::mat4 model;
		glm::mat4 prevModel;
	};
	static_assert(sizeof(PreviewPush) == 128, "Preview push constants must match the GL DrawPush path");

	struct PreviewCameraUBO
	{
		glm::mat4 viewProj;
	};

	void DrawModelPreview(const Model& model, const glm::vec3& boundsMin, const glm::vec3& boundsMax)
	{
		ImGui::TextUnformatted("Preview");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		ImGui::Combo("##previewmode", &m_PreviewMode, "Shaded\0Wireframe\0Normals\0UV\0");
		ImGui::SameLine();
		ImGui::Checkbox("Selected mesh only", &m_PreviewIsolate);
		ImGui::SameLine();
		ImGui::TextDisabled("Left drag orbit, wheel zoom");

		const float previewHeight = 300.0f;
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const uint32_t width = static_cast<uint32_t>(std::max(avail.x, 64.0f));
		const uint32_t height = static_cast<uint32_t>(previewHeight);
		const ImVec2 viewSize(static_cast<float>(width), static_cast<float>(height));

		// Image() does not take mouse capture. A button does, so orbit/zoom can see the drag.
		ImGui::InvisibleButton("##modelview", viewSize);
		HandlePreviewCamera();
		const ImVec2 viewMin = ImGui::GetItemRectMin();

		// Hardware line width cannot go below 1px. Draw the wireframe at 2x
		// and show it in the same rect so the strokes land thinner on screen.
		const uint32_t renderScale = m_PreviewMode == 1 ? 2u : 1u;
		RenderModelPreview(model, boundsMin, boundsMax, width * renderScale, height * renderScale);

		if (m_PreviewTexture != ImTextureID_Invalid)
		{
			ImGui::GetWindowDrawList()->AddImage(
				m_PreviewTexture,
				viewMin,
				ImVec2(viewMin.x + viewSize.x, viewMin.y + viewSize.y),
				ImVec2(0.0f, 1.0f),
				ImVec2(1.0f, 0.0f));
		}
		else
		{
			ImGui::TextUnformatted("Preview unavailable");
		}
	}

	void HandlePreviewCamera()
	{
		ImGuiIO& io = ImGui::GetIO();
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			m_PreviewYaw -= io.MouseDelta.x * 0.01f;
			m_PreviewPitch = std::clamp(m_PreviewPitch + io.MouseDelta.y * 0.01f, -1.2f, 1.2f);
		}
		if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
		{
			const float step = io.MouseWheel > 0.0f ? 0.9f : 1.1f;
			const float minDist = std::max(0.05f, m_PreviewFocusRadius * 0.2f);
			const float maxDist = std::max(1.0f, m_PreviewRadius * 8.0f);
			m_PreviewDistance = std::clamp(m_PreviewDistance * step, minDist, maxDist);
		}
	}

	void RenderModelPreview(
		const Model& model,
		const glm::vec3& boundsMin,
		const glm::vec3& boundsMax,
		uint32_t width,
		uint32_t height)
	{
		if (width == 0 || height == 0)
			return;

		if (!EnsurePreviewPipeline(width, height))
			return;

		if (m_PreviewFramebuffer->GetWidth() != width ||
			m_PreviewFramebuffer->GetHeight() != height)
		{
			m_PreviewFramebuffer->Resize(width, height);
		}

		if (m_PreviewFittedPath != m_SelectedPath)
			FitPreviewCamera(boundsMin, boundsMax);
		else
		{
			const glm::vec3 extent = boundsMax - boundsMin;
			m_PreviewRadius = std::max(0.5f, glm::length(extent) * 0.5f);
		}

		const glm::vec3 modelCenter = (boundsMin + boundsMax) * 0.5f;
		const float pitch = m_PreviewPitch;
		const float yaw = m_PreviewYaw;
		const glm::vec3 eye = m_PreviewTarget + glm::vec3(
			m_PreviewDistance * std::cos(pitch) * std::sin(yaw),
			m_PreviewDistance * std::sin(pitch),
			m_PreviewDistance * std::cos(pitch) * std::cos(yaw));
		const float aspect = static_cast<float>(width) / static_cast<float>(height);
		const float nearPlane = std::max(0.01f, m_PreviewFocusRadius * 0.01f);
		const float farPlane = glm::length(eye - modelCenter) + m_PreviewRadius * 4.0f;
		const glm::mat4 view = glm::lookAt(eye, m_PreviewTarget, glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::mat4 proj = glm::perspective(glm::radians(35.0f), aspect, nearPlane, farPlane);

		PreviewCameraUBO camera{};
		camera.viewProj = proj * view;
		m_PreviewCameraUBO->Upload(&camera, sizeof(camera));

		Ref<RHICommandBuffer> cmd = RHIRenderer::GetCmd();
		if (!cmd)
			return;

		m_PreviewDescriptors->Reset();
		m_PreviewDescriptors->BindUniformBuffer(0, m_PreviewCameraUBO);
		m_PreviewDescriptors->Apply(0);

		RenderPassBeginInfo begin;
		begin.colorAttachments.push_back({});
		begin.colorAttachments[0].loadOp = AttachmentLoadOp::Clear;
		begin.colorAttachments[0].clearValue = { 0.16f, 0.17f, 0.20f, 1.0f };
		begin.hasDepth = true;
		begin.depth.loadOp = AttachmentLoadOp::Clear;
		begin.depth.stencilLoadOp = AttachmentLoadOp::Clear;
		begin.depth.clearDepth = 1.0f;

		cmd->BeginRenderPass(m_PreviewFramebuffer, begin);
		cmd->SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
		cmd->SetScissor({ 0, 0, width, height });
		Ref<RHIPipeline> pipeline = m_PreviewMode == 1 ? m_PreviewWirePipeline : m_PreviewPipeline;
		cmd->BindPipeline(pipeline);
		cmd->BindDescriptorSet(m_PreviewDescriptors, 0);

		const glm::vec4 idleTint(0.62f, 0.66f, 0.72f, 0.0f);
		for (int i = 0; i < (int)model.meshes.size(); ++i)
		{
			const bool selected = i == m_SelectedMesh;
			if (m_PreviewIsolate && !selected)
				continue;
			const Mesh& mesh = model.meshes[i];
			if (!mesh.IsGPUReady())
				continue;

			glm::vec3 color = i < (int)m_MeshColors.size() ? m_MeshColors[i] : idleTint;
			PreviewPush pc{};
			pc.model = glm::mat4(1.0f);
			pc.prevModel = glm::mat4(0.0f);
			pc.prevModel[0] = glm::vec4(color, selected ? 1.0f : 0.0f);
			pc.prevModel[1] = glm::vec4(static_cast<float>(m_PreviewMode), 0.0f, 0.0f, 0.0f);
			cmd->PushConstants(&pc, sizeof(pc));
			mesh.gpuMesh->Draw(*cmd);
		}

		pipeline->Unbind();

		cmd->EndRenderPass();
		if (RHITexture2D* color = m_PreviewFramebuffer->GetColorAttachment(0))
		{
			cmd->PrepareTextureForSampling(color);
			m_PreviewTexture = RHIImGUIRenderer::GetTextureID(color);
		}
	}

	bool EnsurePreviewPipeline(uint32_t width, uint32_t height)
	{
		if (m_PreviewPipeline)
			return true;
		if (m_PreviewFailed)
			return false;

		m_PreviewFramebuffer = RHIFramebuffer::Create(FramebufferDesc{
			width,
			height,
			{ { Format::RGBA8_UNORM, 1 } },
			true,
			1
		});
		if (!m_PreviewFramebuffer)
		{
			m_PreviewFailed = true;
			return false;
		}

		m_PreviewShader = RHIShader::Create("D:/Code/C++/Tsundere/res/shaders/ModelPreview.shader");
		m_PreviewCameraUBO = RHIBuffer::Create({
			sizeof(PreviewCameraUBO), BufferUsage::Uniform, true, nullptr });
		m_PreviewDescriptors = RHIDescriptorSet::Create();
		if (!m_PreviewShader || !m_PreviewCameraUBO || !m_PreviewDescriptors)
		{
			m_PreviewFailed = true;
			return false;
		}

		m_PreviewDescriptors->BindUniformBuffer(0, m_PreviewCameraUBO);

		VertexLayout layout;
		layout.stride = sizeof(Vertex);
		layout.attributes = {
			{ 0, VertexFormat::Float3, static_cast<uint32_t>(offsetof(Vertex, Position)) },
			{ 1, VertexFormat::Float3, static_cast<uint32_t>(offsetof(Vertex, Normal)) },
			{ 2, VertexFormat::Float2, static_cast<uint32_t>(offsetof(Vertex, TexCoords)) },
		};

		PipelineDesc desc;
		desc.shader = m_PreviewShader;
		desc.vertexLayout = layout;
		desc.topology = PrimitiveTopology::Triangles;
		desc.cullMode = CullMode::None;
		desc.depthTest = true;
		desc.depthWrite = true;
		desc.depthOp = CompareOp::Less;
		desc.srcBlend = BlendFactor::One;
		desc.dstBlend = BlendFactor::Zero;
		desc.pushConstantSize = sizeof(PreviewPush);
		desc.descriptorSets = { m_PreviewDescriptors };
		desc.renderingSignature.colorFormats = { m_PreviewFramebuffer->GetColorAttachmentFormat(0) };
		desc.renderingSignature.depthFormat = m_PreviewFramebuffer->GetDepthAttachmentFormat();
		desc.renderingSignature.sampleCount = 1;

		desc.polygonMode = PolygonMode::Fill;
		m_PreviewPipeline = RHIPipeline::Create(desc);
		desc.polygonMode = PolygonMode::Line;
		m_PreviewWirePipeline = RHIPipeline::Create(desc);
		if (!m_PreviewPipeline || !m_PreviewWirePipeline)
		{
			m_PreviewFailed = true;
			return false;
		}
		return true;
	}

	static glm::vec3 DefaultMeshColor(int index)
	{
		const float h = std::fmod(static_cast<float>(index) * 0.61803398875f, 1.0f);
		const float s = 0.55f;
		const float v = 0.86f;
		const float c = v * s;
		const float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
		const float m = v - c;
		glm::vec3 rgb(0.0f);
		switch (static_cast<int>(h * 6.0f) % 6)
		{
		case 0: rgb = glm::vec3(c, x, 0.0f); break;
		case 1: rgb = glm::vec3(x, c, 0.0f); break;
		case 2: rgb = glm::vec3(0.0f, c, x); break;
		case 3: rgb = glm::vec3(0.0f, x, c); break;
		case 4: rgb = glm::vec3(x, 0.0f, c); break;
		default: rgb = glm::vec3(c, 0.0f, x); break;
		}
		return rgb + glm::vec3(m);
	}

	void EnsureMeshColors(const Model& model)
	{
		if (m_MeshColors.size() > model.meshes.size())
			m_MeshColors.resize(model.meshes.size());
		while (m_MeshColors.size() < model.meshes.size())
			m_MeshColors.push_back(DefaultMeshColor(static_cast<int>(m_MeshColors.size())));
	}

	void FitPreviewCamera(const glm::vec3& boundsMin, const glm::vec3& boundsMax)
	{
		const glm::vec3 extent = boundsMax - boundsMin;
		m_PreviewRadius = std::max(0.5f, glm::length(extent) * 0.5f);
		m_PreviewFocusRadius = m_PreviewRadius;
		m_PreviewTarget = (boundsMin + boundsMax) * 0.5f;
		m_PreviewDistance = m_PreviewRadius * 2.6f;
		m_PreviewYaw = 0.6f;
		m_PreviewPitch = 0.25f;
		m_PreviewFittedPath = m_SelectedPath;
	}

	void FocusPreviewOnMesh(const Mesh& mesh)
	{
		bool any = false;
		glm::vec3 bmin(FLT_MAX);
		glm::vec3 bmax(-FLT_MAX);
		for (const auto& vertex : mesh.vertices)
		{
			bmin = glm::min(bmin, vertex.Position);
			bmax = glm::max(bmax, vertex.Position);
			any = true;
		}
		if (!any)
			return;

		const float radius = std::max(0.05f, glm::length(bmax - bmin) * 0.5f);
		m_PreviewTarget = (bmin + bmax) * 0.5f;
		m_PreviewFocusRadius = radius;
		m_PreviewDistance = radius * 2.6f;
	}

	void DeleteSelectedMesh(Model& model)
	{
		if (m_SelectedMesh < 0 || m_SelectedMesh >= (int)model.meshes.size())
			return;

		const int removed = m_SelectedMesh;
		Engine::ResourceLoader::NotifyMeshRemoved(
			My_map::GetModel(m_SelectedPath), static_cast<size_t>(removed));
		model.meshes.erase(model.meshes.begin() + removed);
		if (removed < (int)m_MeshColors.size())
			m_MeshColors.erase(m_MeshColors.begin() + removed);

		if (m_Scene)
		{
			auto view = m_Scene->m_Registry.view<Component::MeshRender>();
			for (auto entity : view)
			{
				auto& meshrender = view.get<Component::MeshRender>(entity);
				if (meshrender.ModelPath != m_SelectedPath)
					continue;
				if (removed < (int)meshrender.materials.size())
					meshrender.materials.erase(meshrender.materials.begin() + removed);
			}
		}

		if (m_SelectedMesh >= (int)model.meshes.size())
			m_SelectedMesh = (int)model.meshes.size() - 1;
		if (m_SelectedMesh < 0)
			m_SelectedMesh = 0;
		BVHBuilder::MarkActiveDirty();
	}

	void DestroySelf()
	{
		Engine::Application::Get().PopLayer(this);
		delete this;
	}

	static inline ModelEditorPanel* s_Instance = nullptr;
	static inline Ref<Scene> s_Scene;

	Ref<Scene> m_Scene;
	std::string m_SelectedPath;
	bool m_Open = false;
	int m_SelectedMesh = 0;
	char m_BoneFilter[64] = {};

	Ref<RHIFramebuffer> m_PreviewFramebuffer;
	Ref<RHIShader> m_PreviewShader;
	Ref<RHIPipeline> m_PreviewPipeline;
	Ref<RHIPipeline> m_PreviewWirePipeline;
	Ref<RHIBuffer> m_PreviewCameraUBO;
	Ref<RHIDescriptorSet> m_PreviewDescriptors;
	ImTextureID m_PreviewTexture = ImTextureID_Invalid;
	std::string m_PreviewFittedPath;
	float m_PreviewYaw = 0.6f;
	float m_PreviewPitch = 0.25f;
	float m_PreviewDistance = 3.0f;
	float m_PreviewRadius = 1.0f;
	float m_PreviewFocusRadius = 1.0f;
	glm::vec3 m_PreviewTarget{ 0.0f };
	bool m_PreviewIsolate = false;
	bool m_PreviewFailed = false;
	int m_PreviewMode = 0;
	std::vector<glm::vec3> m_MeshColors;
};
