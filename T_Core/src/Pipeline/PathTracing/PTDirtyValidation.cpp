#include "PTDirtyValidation.h"

#include "Scene/Scene.h"
#include "Scene/Modle.h"
#include "Panels/MeshFilePath.h"
#include "Core/Material.h"
#include "Debug/Debug.h"

#include <cstring>
#include <type_traits>
#include <variant>

namespace
{
	// 64 位按字混合：只用于变化检测，顶点数据量大，不逐字节 FNV。
	struct Hasher
	{
		uint64_t value = 0x9E3779B97F4A7C15ull;

		void Word(uint64_t w)
		{
			value ^= w + 0x9E3779B97F4A7C15ull + (value << 6) + (value >> 2);
			value *= 0xFF51AFD7ED558CCDull;
			value ^= value >> 32;
		}
		void Bytes(const void* data, size_t size)
		{
			const auto* p = static_cast<const unsigned char*>(data);
			size_t i = 0;
			for (; i + 8 <= size; i += 8)
			{
				uint64_t w;
				std::memcpy(&w, p + i, 8);
				Word(w);
			}
			uint64_t tail = 0;
			std::memcpy(&tail, p + i, size - i);
			Word(tail ^ (uint64_t(size) << 56));
		}
		template<typename T>
		void Value(const T& v)
		{
			static_assert(std::is_trivially_copyable<T>::value, "hash trivially copyable values only");
			Bytes(&v, sizeof(T));
		}
		void String(const std::string& s) { Bytes(s.data(), s.size()); }
	};

	uint64_t HashMeshContent(const Mesh& mesh)
	{
		// 只覆盖 PT 实际使用的属性：位置、法线、UV、索引
		Hasher h;
		h.Value(mesh.vertices.size());
		for (const Vertex& v : mesh.vertices)
		{
			h.Value(v.Position);
			h.Value(v.Normal);
			h.Value(v.TexCoords);
		}
		h.Value(mesh.indices.size());
		if (!mesh.indices.empty())
			h.Bytes(mesh.indices.data(), mesh.indices.size() * sizeof(mesh.indices[0]));
		return h.value;
	}

	uint64_t HashMaterial(const Engine::Material& material)
	{
		// unordered_map 的遍历顺序可能因 rehash 改变：逐项哈希后求和，与顺序无关
		uint64_t sum = 0;
		for (const auto& [name, parameter] : material.GetParameters())
		{
			Hasher h;
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
			sum += h.value;
		}
		Hasher h;
		h.Value(sum);
		h.Value(material.GetParameters().size());
		h.Value(material.GetShader().get());
		return h.value;
	}

	std::string EntityLabel(const Scene& scene, uint32_t id)
	{
		const auto entity = static_cast<entt::entity>(id);
		std::string label = "entity " + std::to_string(id);
		if (scene.m_Registry.valid(entity))
			if (const auto* tag = scene.m_Registry.try_get<Component::Tag>(entity))
				label += " '" + tag->tag + "'";
		return label;
	}
}

PTDirtyValidator::Baseline PTDirtyValidator::Capture(const Scene& scene) const
{
	Baseline b;
	const auto& registry = scene.m_Registry;

	for (auto [entity, transform, meshrender] :
		registry.view<const Component::Transform, const Component::MeshRender>().each())
	{
		const uint32_t id = static_cast<uint32_t>(entity);
		{
			Hasher h;
			h.Value(transform.GetTransform());
			b.transforms[id] = h.value;
		}
		// GetModel 对未加载的路径会发起加载请求 —— 与 PT 采集时的行为一致
		const Ref<Model> model = meshrender.ModelPath.empty() ? nullptr : My_map::GetModel(meshrender.ModelPath);
		{
			Hasher h;
			h.Value(scene.IsActive(entity));
			h.String(meshrender.ModelPath);
			h.Value(model.get());
			if (model)
			{
				h.Value(model->meshes.size());
				for (const Mesh& mesh : model->meshes)
				{
					h.Value(mesh.GeometryId.Get());
					h.Value(mesh.Active);
				}
			}
			b.instanceSets[id] = h.value;
		}
		{
			Hasher h;
			h.Value(meshrender.materials.size());
			for (const auto& material : meshrender.materials)
				h.Value(material.get());
			b.assignments[id] = h.value;
		}
		if (model)
		{
			for (size_t i = 0; i < model->meshes.size(); ++i)
			{
				const Mesh& mesh = model->meshes[i];
				auto [it, inserted] = b.meshes.try_emplace(mesh.GeometryId.Get());
				if (!inserted)
					continue;
				it->second.revision = mesh.GeometryRevision;
				it->second.hash = HashMeshContent(mesh);
				it->second.label = meshrender.ModelPath + " #" + std::to_string(i);
			}
		}
		for (const auto& material : meshrender.materials)
		{
			if (!material)
				continue;
			auto [it, inserted] = b.materials.try_emplace(material.get());
			if (inserted)
				it->second = MaterialRecord{ material->GetRevision(), HashMaterial(*material) };
		}
	}

	for (auto [entity, transform, light] :
		registry.view<const Component::Transform, const Component::Light>().each())
	{
		Hasher h;
		h.Value(scene.IsActive(entity));
		h.Value(transform.Position);
		h.Value(light.type);
		h.Value(light.Color);
		h.Value(light.Intensity);
		h.Value(light.Direction);
		h.Value(light.Ambient);
		h.Value(light.Params.Data);
		b.lights[static_cast<uint32_t>(entity)] = h.value;
	}
	return b;
}

PTDirtyValidator::Report PTDirtyValidator::Check(const Scene& scene)
{
	using SceneChanges::Category;
	Report report;
	Baseline current = Capture(scene);
	current.versions = SceneChanges::Snapshot();

	if (!m_HasBaseline)
	{
		m_Baseline = std::move(current);
		m_HasBaseline = true;
		return report;
	}

	const uint32_t notified = current.versions.ChangedSince(m_Baseline.versions);
	const auto covered = [notified](Category c) { return (notified & SceneChanges::Bit(c)) != 0; };
	const auto miss = [&](Category c, const std::string& what)
	{
		report.missedMask |= SceneChanges::Bit(c);
		report.messages.push_back(std::string(SceneChanges::CategoryName(c)) + ": " + what);
	};

	// 逐实体比较：changed（内容变了）且没有对应通知 → 漏标。setChanged = 实体集合有增删。
	const auto compareEntities = [&](const std::unordered_map<uint32_t, uint64_t>& before,
		const std::unordered_map<uint32_t, uint64_t>& after, Category category, bool setMatters, const char* what)
	{
		if (covered(category))
			return;
		for (const auto& [id, hash] : after)
		{
			const auto old = before.find(id);
			if (old == before.end())
			{
				if (setMatters)
					miss(category, EntityLabel(scene, id) + " added without notification");
			}
			else if (old->second != hash)
				miss(category, EntityLabel(scene, id) + " " + what + " changed without notification");
		}
		if (setMatters)
			for (const auto& [id, hash] : before)
				if (!after.count(id))
					miss(category, "entity " + std::to_string(id) + " removed without notification");
	};
	compareEntities(m_Baseline.transforms, current.transforms, Category::InstanceTransform, false, "transform");
	compareEntities(m_Baseline.instanceSets, current.instanceSets, Category::InstanceSet, true, "model / activation / submesh state");
	compareEntities(m_Baseline.assignments, current.assignments, Category::MaterialAssignment, false, "material slots");
	compareEntities(m_Baseline.lights, current.lights, Category::Lights, true, "light");

	// Mesh 内容：以 Mesh 自身的几何版本为通知依据
	std::vector<uint64_t> missedMeshes;
	for (const auto& [id, record] : current.meshes)
	{
		const auto old = m_Baseline.meshes.find(id);
		if (old != m_Baseline.meshes.end() && old->second.revision == record.revision && old->second.hash != record.hash)
		{
			miss(Category::MeshGeometry, "mesh " + record.label + " vertices / indices changed without NotifyMeshGeometry");
			missedMeshes.push_back(id);
		}
	}

	// 材质参数：以 Material 自身的版本为通知依据（所有 setter 自动 MarkChanged）
	std::vector<const Engine::Material*> missedMaterials;
	for (const auto& [material, record] : current.materials)
	{
		const auto old = m_Baseline.materials.find(material);
		if (old != m_Baseline.materials.end() && old->second.revision == record.revision && old->second.hash != record.hash)
		{
			miss(Category::MaterialParameters, "material '" + material->GetName() +
				"' parameter written through GetParameter() without MarkChanged()");
			missedMaterials.push_back(material);
		}
	}

	if (report.missedMask)
	{
		// ---- 自愈：补发通知，本帧的增量更新照常拿到正确数据 ----
		if (!missedMeshes.empty() || !missedMaterials.empty())
		{
			for (auto [entity, meshrender] : scene.m_Registry.view<const Component::MeshRender>().each())
			{
				if (!missedMeshes.empty() && !meshrender.ModelPath.empty())
					if (const Ref<Model> model = My_map::GetModel(meshrender.ModelPath))
						for (Mesh& mesh : model->meshes)
							for (uint64_t& id : missedMeshes)
								if (id != 0 && mesh.GeometryId.Get() == id)
								{
									SceneChanges::NotifyMeshGeometry(mesh);
									current.meshes[id].revision = mesh.GeometryRevision;
									id = 0;
								}
				for (const auto& material : meshrender.materials)
					for (const Engine::Material*& missed : missedMaterials)
						if (missed && material.get() == missed)
						{
							material->MarkChanged();
							current.materials[missed].revision = material->GetRevision();
							missed = nullptr;
						}
			}
		}
		SceneChanges::NotifyMask(report.missedMask &
			~(SceneChanges::Bit(Category::MeshGeometry) | SceneChanges::Bit(Category::MaterialParameters)));
		current.versions = SceneChanges::Snapshot();

		m_MissedTotal += report.messages.size();
		m_LastReport.clear();
		for (const std::string& message : report.messages)
		{
			Error_Core("[PT dirty validation] missed notification: {}", message);
			if (!m_LastReport.empty())
				m_LastReport += '\n';
			m_LastReport += message;
		}
	}

	m_Baseline = std::move(current);
	return report;
}
