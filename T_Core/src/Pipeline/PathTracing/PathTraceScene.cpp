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
	, m_Validator(std::make_unique<PTDirtyValidator>())
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
}

PathTraceScene::~PathTraceScene() = default;

bool PathTraceScene::IsReady() const
{
	if (!m_Geometry.IsGPUReady() || !m_MaterialBuffer || !m_LightBuffer)
		return false;
	return m_Backend == Backend::SoftwareBVH || (m_RayTracing && m_RayTracing->IsValid());
}

void PathTraceScene::SetValidation(bool enabled, uint32_t interval)
{
	const bool enable = ValidationCompiled && enabled;
	// 关闭期间的修改不该被当成漏标：重新打开时从当前状态建立基线
	if (enable && !m_ValidationEnabled && m_Validator)
		m_Validator->Reset();
	m_ValidationEnabled = enable;
	m_ValidationInterval = std::max(interval, 1u);
}

uint64_t PathTraceScene::GetMissedNotificationTotal() const
{
	return m_Validator ? m_Validator->GetMissedTotal() : 0;
}

const std::string& PathTraceScene::GetLastValidationReport() const
{
	static const std::string kEmpty;
	return m_Validator ? m_Validator->GetLastReport() : kEmpty;
}

bool PathTraceScene::RebuildMaterialTable(const Scene& scene)
{
	// 0 = 默认材质。顺序确定（registry 遍历顺序），只有指针序列变了才算变化。
	std::vector<Ref<Engine::Material>> refs;
	std::unordered_map<Engine::Material*, unsigned int> index;
	refs.push_back(nullptr);
	for (auto [entity, meshrender] : scene.m_Registry.view<const Component::MeshRender>().each())
	{
		for (const auto& material : meshrender.materials)
		{
			if (!material || index.count(material.get()))
				continue;
			index[material.get()] = static_cast<unsigned int>(refs.size());
			refs.push_back(material);
		}
	}

	bool same = refs.size() == m_MaterialRefs.size();
	for (size_t i = 0; same && i < refs.size(); ++i)
		same = refs[i] == m_MaterialRefs[i];
	if (same)
		return false;
	m_MaterialRefs = std::move(refs);
	m_MaterialIndex = std::move(index);
	return true;
}

bool PathTraceScene::UpdateGeometry(const Scene& scene, UpdateInfo& info)
{
	m_Geometry.SetMaterialMap(&m_MaterialIndex);
	info.geometry = m_Geometry.UpdateIncremental(scene);
	m_Geometry.SetMaterialMap(nullptr);
	const BVHIncrementalResult& result = info.geometry;
	if (!result.Changed())
		return false;

	const BVHStats& stats = m_Geometry.GetStats();
	if (m_Backend == Backend::SoftwareBVH &&
		(stats.tlasDepth > ShaderTLASStack || stats.blasDepth > ShaderBLASStack))
	{
		// 构建器按设置钳了深度，这里只是防御：深度超出着色器栈就不能运行
		m_Status = "BVH deeper than the shader traversal stack";
		m_Failed = true;
	}

	// 硬件加速结构：只改材质下标时不需要动（materialIndex 在实例 SSBO 里）
	if (!result.uploadFailed && m_Backend == Backend::RayQuery && m_RayTracing &&
		(result.geometryChanged || result.instancesChanged))
	{
		info.accelUpdated = true;
		if (!m_RayTracing->Update(m_Geometry.GetData(), m_Geometry.GetBLASKeys()))
		{
			m_Failed = true;
			m_Status = "acceleration structure build failed";
		}
	}
	++m_GeometryRevision;
	return true;
}

bool PathTraceScene::UpdateMaterials()
{
	std::vector<GPUMaterial> materials;
	materials.reserve(std::max<size_t>(m_MaterialRefs.size(), 1));
	for (const auto& material : m_MaterialRefs)
		materials.push_back(ExtractMaterial(material.get()));
	if (materials.empty())
		materials.emplace_back();

	// 改的参数 PT 不用（贴图、透明度……）时提取结果不变：不上传、不重置累积
	const bool same = m_MaterialBuffer && materials.size() == m_Materials.size() &&
		std::memcmp(materials.data(), m_Materials.data(), materials.size() * sizeof(GPUMaterial)) == 0;
	if (same)
		return false;
	m_Materials = std::move(materials);
	UploadArray(m_MaterialBuffer, m_MaterialCapacity, m_Materials);
	return true;
}

bool PathTraceScene::UpdateLights(const Scene& scene)
{
	std::vector<GPULight> lights;
	for (auto [entity, transform, light] :
		scene.m_Registry.view<const Component::Transform, const Component::Light>().each())
	{
		if (!scene.IsActive(entity))
			continue;   // 与延迟光照 / 局部光一致：停用的实体不发光
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

void PathTraceScene::RefreshStatus()
{
	if (m_Failed)
		return;
	const BVHStats& s = m_Geometry.GetStats();
	m_Status = std::to_string(s.instanceCount) + " instances, " + std::to_string(s.meshCount) +
		" meshes, " + std::to_string(s.triangleCount) + " triangles, " +
		std::to_string(m_Materials.size()) + " materials, " + std::to_string(m_Lights.size()) + " lights";
}

bool PathTraceScene::Update(const Scene& scene)
{
	using SceneChanges::Bit;
	using SceneChanges::Category;
	if (m_Failed)
		return false;

	// 换了场景对象 / 场景被整体替换（加载）：全部类别视为脏
	const bool sceneSwapped = !m_HasSeen || m_LastScene != &scene || m_LastSceneRevision != scene.Revision;
	if (sceneSwapped)
	{
		m_LastScene = &scene;
		m_LastSceneRevision = scene.Revision;
		if (m_Validator)
			m_Validator->Reset();
	}

	// ---- 开发期校验：在读取 / 消费版本号之前（自愈会补发通知，下面就能看到）----
	uint32_t missed = 0;
#if ENABLE_PT_DIRTY_VALIDATION
	if (m_ValidationEnabled && m_Validator)
	{
		if (m_ValidationCountdown == 0)
		{
			missed = m_Validator->Check(scene).missedMask;
			m_ValidationCountdown = m_ValidationInterval;
		}
		--m_ValidationCountdown;
	}
#endif

	// ---- 取快照后再处理：处理期间发生的修改让版本继续前进，下一帧照常看到 ----
	const SceneChanges::Versions now = SceneChanges::Snapshot();
	const uint32_t dirty = sceneSwapped ? SceneChanges::AllCategories : now.ChangedSince(m_Seen);
	m_Seen = now;
	m_HasSeen = true;
	if (dirty == 0)
		return false;   // 静态场景：不扫描任何东西

	const auto start = std::chrono::steady_clock::now();
	UpdateInfo info;
	info.dirtyMask = dirty;
	info.missedMask = missed;

	// 材质表依赖哪些实体 / 槽位在场景里
	bool tableChanged = false;
	if (dirty & (Bit(Category::InstanceSet) | Bit(Category::MaterialAssignment)))
		tableChanged = RebuildMaterialTable(scene);

	bool changed = false;
	if ((dirty & SceneChanges::GeometryCategories) || tableChanged)
		changed |= UpdateGeometry(scene, info);
	if (tableChanged || (dirty & Bit(Category::MaterialParameters)) || !m_MaterialBuffer)
		changed |= info.materialsUploaded = UpdateMaterials();
	if ((dirty & Bit(Category::Lights)) || !m_LightBuffer)
		changed |= info.lightsUploaded = UpdateLights(scene);

	info.resetAccumulation = changed;
	info.cpuMilliseconds = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
	m_LastUpdate = info;
	RefreshStatus();
	return changed;
}
