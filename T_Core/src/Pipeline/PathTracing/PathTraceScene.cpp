#include "PathTraceScene.h"

#include "Scene/Scene.h"
#include "Panels/MeshFilePath.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <type_traits>
#include <variant>

using namespace PathTracing;

namespace
{
	// FNV-1a，只用于变化检测
	struct Hasher
	{
		uint64_t value = 1469598103934665603ull;
		void Bytes(const void* data, size_t size)
		{
			const auto* p = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; ++i)
			{
				value ^= p[i];
				value *= 1099511628211ull;
			}
		}
		template<typename T>
		void Value(const T& v) { Bytes(&v, sizeof(T)); }
		void String(const std::string& s) { Bytes(s.data(), s.size()); Value(s.size()); }
	};

	using MaterialParameter = Engine::MaterialParameter;

	const MaterialParameter* FindParameter(const Engine::Material* material,
		std::initializer_list<const char*> names)
	{
		for (const char* name : names)
			if (const MaterialParameter* p = material->GetParameter(name))
				return p;
		return nullptr;
	}

	bool ReadColor(const MaterialParameter* p, glm::vec3& out)
	{
		if (!p)
			return false;
		if (const auto* v = p->Get<glm::vec4>()) { out = glm::vec3(*v); return true; }
		if (const auto* v = p->Get<glm::vec3>()) { out = *v; return true; }
		if (p->type == MaterialParameter::Type::Float)
			if (const auto* v = p->Get<float>()) { out = glm::vec3(*v); return true; }
		return false;
	}

	float ReadFloat(const MaterialParameter* p, float fallback)
	{
		if (!p)
			return fallback;
		if (const auto* v = p->Get<float>()) return *v;
		if (const auto* v = p->Get<int>()) return static_cast<float>(*v);
		return fallback;
	}

	template<typename T>
	void UploadArray(Ref<RHIBuffer>& buffer, uint32_t& capacity, const std::vector<T>& data)
	{
		const uint32_t count = static_cast<uint32_t>(std::max<size_t>(data.size(), 1));
		if (!buffer || capacity < count)
		{
			capacity = std::max(count, capacity * 2);
			buffer = RHIBuffer::Create(BufferDesc{ capacity * static_cast<uint32_t>(sizeof(T)),
				BufferUsage::Storage, true, nullptr });
		}
		if (!data.empty())
			buffer->Upload(data.data(), static_cast<uint32_t>(data.size() * sizeof(T)));
		else
		{
			T zero{};
			buffer->Upload(&zero, sizeof(T));
		}
	}
}

namespace PathTracing
{
	GPUMaterial ExtractMaterial(const Engine::Material* material)
	{
		GPUMaterial out;
		if (!material)
			return out;

		glm::vec3 color(0.8f);
		if (ReadColor(FindParameter(material, { "u_BaseColor", "u_Color", "baseColor", "albedo", "color", "diffuseColor" }), color))
			out.baseColor = glm::vec4(glm::max(color, glm::vec3(0.0f)), 1.0f);

		glm::vec3 emissive(0.0f);
		ReadColor(FindParameter(material, { "u_EmissiveColor", "emission", "emissive" }), emissive);
		const float strength = ReadFloat(FindParameter(material, { "u_EmissiveStrength" }), 1.0f);
		out.emission = glm::vec4(glm::max(emissive * strength, glm::vec3(0.0f)), 0.0f);

		out.roughness = glm::clamp(ReadFloat(FindParameter(material, { "u_Roughness", "roughness" }), 0.5f), 0.0f, 1.0f);
		out.metallic = glm::clamp(ReadFloat(FindParameter(material, { "u_Metallic", "metallic" }), 0.0f), 0.0f, 1.0f);
		out.type = out.metallic >= 0.5f ? MaterialMetal : MaterialDiffuse;
		return out;
	}
}

PathTraceScene::PathTraceScene(Backend backend)
	: m_Backend(backend)
{
	BVHBuildSettings& settings = m_Geometry.GetSettings();
	settings.buildTrees = backend == Backend::SoftwareBVH;
	// 着色器遍历栈是定长数组：树深上限必须不超过它（见 BVH.glsl）
	settings.maxTLASDepth = std::min(settings.maxTLASDepth, ShaderTLASStack);
	settings.maxBLASDepth = std::min(settings.maxBLASDepth, ShaderBLASStack);

	if (backend == Backend::RayQuery)
	{
		m_RayTracing = RHIRayTracingScene::Create();
		if (!m_RayTracing)
		{
			m_Failed = true;
			m_Status = RHIRayTracingScene::GetUnsupportedReason();
		}
	}
	// 让模型加载 / 编辑器删网格能标脏这份几何
	BVHBuilder::SetActiveInstance(&m_Geometry);
}

PathTraceScene::~PathTraceScene()
{
	if (BVHBuilder::GetActiveInstance() == &m_Geometry)
		BVHBuilder::SetActiveInstance(nullptr);
}

bool PathTraceScene::IsReady() const
{
	if (!m_Geometry.IsGPUReady() || !m_MaterialBuffer || !m_LightBuffer)
		return false;
	return m_Backend == Backend::SoftwareBVH || (m_RayTracing && m_RayTracing->IsValid());
}

uint64_t PathTraceScene::HashGeometry(const Scene& scene) const
{
	Hasher h;
	for (auto [entity, transform, meshrender] :
		scene.m_Registry.view<const Component::Transform, const Component::MeshRender>().each())
	{
		if (meshrender.ModelPath.empty())
			continue;
		h.Value(entity);
		h.String(meshrender.ModelPath);
		const Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
		h.Value(model.get());
		if (model)
		{
			h.Value(model->meshes.size());
			for (const Mesh& mesh : model->meshes)
			{
				h.Value(&mesh);
				h.Value(mesh.vertices.size());
				h.Value(mesh.indices.size());
			}
		}
		const glm::mat4 m = transform.GetTransform();
		h.Value(m);
		h.Value(meshrender.materials.size());
		for (const auto& material : meshrender.materials)
			h.Value(material.get());
	}
	return h.value;
}

uint64_t PathTraceScene::HashMaterialParameters() const
{
	Hasher h;
	for (const auto& material : m_MaterialRefs)
	{
		if (!material)
			continue;
		h.Value(material.get());
		for (const auto& [name, parameter] : material->GetParameters())
		{
			h.String(name);
			h.Value(parameter.type);
			std::visit([&h](const auto& v)
			{
				using T = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<T, Ref<RHITexture2D>>)
					h.Value(v.get());
				else
					h.Value(v);
			}, parameter.value);
		}
	}
	return h.value;
}

void PathTraceScene::RebuildGeometry(const Scene& scene)
{
	const auto start = std::chrono::steady_clock::now();

	// 1) 材质表（0 = 默认）
	m_MaterialIndex.clear();
	m_MaterialRefs.clear();
	m_MaterialRefs.push_back(nullptr);
	for (auto [entity, meshrender] : scene.m_Registry.view<const Component::MeshRender>().each())
	{
		for (const auto& material : meshrender.materials)
		{
			if (!material || m_MaterialIndex.count(material.get()))
				continue;
			m_MaterialIndex[material.get()] = static_cast<unsigned int>(m_MaterialRefs.size());
			m_MaterialRefs.push_back(material);
		}
	}

	// 2) 几何：采集 → 构建（树或平铺）→ 上传
	m_Geometry.MarkClean();   // 构建期间再发生的修改会重新标脏
	m_Geometry.SetMaterialMap(&m_MaterialIndex);
	m_Geometry.GatherScene(scene);
	m_Geometry.SetMaterialMap(nullptr);
	m_Geometry.BuildBVH(m_Geometry.GetSettings().maxLeafSize);
	const bool uploaded = m_Geometry.UploadToGPU();

	const BVHStats& stats = m_Geometry.GetStats();
	if (m_Backend == Backend::SoftwareBVH &&
		(stats.tlasDepth > ShaderTLASStack || stats.blasDepth > ShaderBLASStack))
	{
		// 构建器按设置钳了深度，这里只是防御：深度超出着色器栈就不能运行
		m_Status = "BVH deeper than the shader traversal stack";
		m_Failed = true;
	}

	// 3) 硬件加速结构
	if (uploaded && m_Backend == Backend::RayQuery && m_RayTracing)
	{
		if (!m_RayTracing->Build(m_Geometry.GetData()))
		{
			m_Failed = true;
			m_Status = "acceleration structure build failed";
		}
	}

	m_GatherMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
	m_HasGeometry = uploaded;
	++m_GeometryRevision;
	m_MaterialHash = 0;   // 强制重传材质（索引可能变了）
}

bool PathTraceScene::UpdateMaterials()
{
	const uint64_t hash = HashMaterialParameters();
	if (hash == m_MaterialHash && m_MaterialBuffer)
		return false;
	m_MaterialHash = hash;

	m_Materials.clear();
	m_Materials.reserve(m_MaterialRefs.size());
	for (const auto& material : m_MaterialRefs)
		m_Materials.push_back(ExtractMaterial(material.get()));
	if (m_Materials.empty())
		m_Materials.emplace_back();
	UploadArray(m_MaterialBuffer, m_MaterialCapacity, m_Materials);
	return true;
}

bool PathTraceScene::UpdateLights(const Scene& scene)
{
	std::vector<GPULight> lights;
	for (auto [entity, transform, light] :
		scene.m_Registry.view<const Component::Transform, const Component::Light>().each())
	{
		GPULight g;
		g.directionAmbient = glm::vec4(light.Direction, light.Ambient);
		g.colorIntensity = glm::vec4(light.Color, light.Intensity);
		g.positionType = glm::vec4(transform.Position, static_cast<float>(light.type));
		g.params = glm::vec4(light.Params.Data[0], light.Params.Data[1], light.Params.Data[2], light.Params.Data[3]);
		if (light.type == Component::Light::LightType::Area)
			g.params.z = static_cast<float>(light.Params.Area.Shape);
		lights.push_back(g);
	}

	const bool same = m_LightBuffer && lights.size() == m_Lights.size() &&
		(lights.empty() || std::memcmp(lights.data(), m_Lights.data(), lights.size() * sizeof(GPULight)) == 0);
	if (same)
		return false;
	m_Lights = std::move(lights);
	UploadArray(m_LightBuffer, m_LightCapacity, m_Lights);
	return true;
}

bool PathTraceScene::Update(const Scene& scene)
{
	if (m_Failed)
		return false;

	bool changed = false;
	const uint64_t geometryHash = HashGeometry(scene);
	if (!m_HasGeometry || geometryHash != m_GeometryHash || m_Geometry.IsDirty())
	{
		m_GeometryHash = geometryHash;
		RebuildGeometry(scene);
		changed = true;
	}
	changed |= UpdateMaterials();
	changed |= UpdateLights(scene);

	if (!m_Failed)
	{
		const BVHStats& s = m_Geometry.GetStats();
		m_Status = std::to_string(s.instanceCount) + " instances, " + std::to_string(s.meshCount) +
			" meshes, " + std::to_string(s.triangleCount) + " triangles, " +
			std::to_string(m_Materials.size()) + " materials, " + std::to_string(m_Lights.size()) + " lights";
	}
	return changed;
}
