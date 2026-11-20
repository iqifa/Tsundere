#include "BVHBuilder.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <string>
#include <unordered_set>

namespace
{
	BVHMeshInput CopyMeshInput(const Mesh& mesh)
	{
		BVHMeshInput input;
		const size_t vertexCount = mesh.vertices.size();
		input.positions.resize(vertexCount);
		input.normals.resize(vertexCount);
		input.uvs.resize(vertexCount);
		for (size_t v = 0; v < vertexCount; ++v)
		{
			const Vertex& vertex = mesh.vertices[v];
			input.positions[v] = vertex.Position;
			input.normals[v] = vertex.Normal;
			input.uvs[v] = vertex.TexCoords;
		}
		input.indices.assign(mesh.indices.begin(), mesh.indices.end());
		return input;
	}

	bool UsableMesh(const Mesh& mesh)
	{
		return mesh.Active && !mesh.vertices.empty() && mesh.indices.size() >= 3;
	}

	template<typename T>
	Ref<RHIBuffer> CreateStorageBuffer(const std::vector<T>& elements)
	{
		const size_t bytes = elements.size() * sizeof(T);
		if (bytes == 0 || bytes > UINT32_MAX) // BufferDesc::size 是 uint32_t
			return nullptr;
		return RHIBuffer::Create(BufferDesc{
			static_cast<uint32_t>(bytes), BufferUsage::Storage, false, elements.data() });
	}

	bool SameInstanceGeometry(const BVHInstanceInput& a, const BVHInstanceInput& b)
	{
		return a.meshIndex == b.meshIndex && a.entityID == b.entityID &&
			std::memcmp(&a.objectToWorld, &b.objectToWorld, sizeof(glm::mat4)) == 0;
	}
}

uint32_t BVHBuilder::LookupMaterial(const Component::MeshRender& meshrender, size_t meshIdx) const
{
	if (!m_MaterialMap || meshIdx >= meshrender.materials.size())
		return 0;
	const auto found = m_MaterialMap->find(meshrender.materials[meshIdx].get());
	return found != m_MaterialMap->end() ? found->second : 0;
}

// ===========================================================================
// 增量路径
// ===========================================================================

void BVHBuilder::ResetIncremental()
{
	m_BLASCache.clear();
	m_BLASOrder.clear();
	m_BLASKeys.clear();
	m_BLASHints.clear();
	m_LastInstances.clear();
	m_IncrementalValid = false;
}

BVHIncrementalResult BVHBuilder::UpdateIncremental(const Scene& scene, bool upload)
{
	BVHIncrementalResult result;
	const auto start = std::chrono::steady_clock::now();

	for (auto& [id, entry] : m_BLASCache)
	{
		entry.used = false;
		entry.refitFrom = 0;
	}

	// ---- 1) 遍历实例；缓存缺失 / 几何版本变化的网格才拷贝顶点并构建 BLAS ----
	std::vector<BVHInstanceInput> inputs;
	std::vector<uint64_t> inputGeometry;   // inputs[i] 的 GeometryId
	std::vector<uint64_t> newGeometry;     // 本次新出现的 GeometryId（首次出现顺序）
	for (auto [entityID, transform, meshrender] :
		scene.m_Registry.view<const Component::Transform, const Component::MeshRender>().each())
	{
		if (!scene.IsActive(entityID) || meshrender.ModelPath.empty())
			continue;
		const Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
		if (!model || model->meshes.empty())
			continue; // 还在加载：CompleteModelLoad 会通知 InstanceSet

		const glm::mat4 objectToWorld = transform.GetTransform();
		for (size_t meshIdx = 0; meshIdx < model->meshes.size(); ++meshIdx)
		{
			const Mesh& mesh = model->meshes[meshIdx];
			if (!UsableMesh(mesh))
				continue;

			const uint64_t geometryId = mesh.GeometryId.Get();
			auto it = m_BLASCache.find(geometryId);
			if (it == m_BLASCache.end())
			{
				CachedBLAS entry;
				entry.revision = mesh.GeometryRevision;
				entry.key = m_NextBLASKey++;
				BuildMeshBLAS(CopyMeshInput(mesh), m_Settings, entry.piece);
				it = m_BLASCache.emplace(geometryId, std::move(entry)).first;
				newGeometry.push_back(geometryId);
				++result.blasBuilt;
			}
			else if (it->second.revision != mesh.GeometryRevision)
			{
				// 几何改了：拓扑不变且树还没退化太多就 refit，否则重建
				CachedBLAS& entry = it->second;
				const BVHMeshInput input = CopyMeshInput(mesh);
				const uint64_t oldKey = entry.key;
				bool refitted = false;
				if (m_Settings.blasRefit && entry.piece.refitCount < m_Settings.maxBLASRefits &&
					RefitMeshBLAS(input, entry.piece))
				{
					refitted = !(entry.piece.buildCost > 0.0f) ||
						entry.piece.cost <= entry.piece.buildCost * m_Settings.blasRefitCostRatio;
				}
				if (!refitted)
					BuildMeshBLAS(input, m_Settings, entry.piece);
				entry.revision = mesh.GeometryRevision;
				entry.key = m_NextBLASKey++;
				entry.refitFrom = refitted ? oldKey : 0;
				entry.dynamic = true;
				++(refitted ? result.blasRefitted : result.blasBuilt);
			}
			else if (!it->second.used)
			{
				++result.blasReused;
			}
			it->second.used = true;

			BVHInstanceInput instance;
			instance.objectToWorld = objectToWorld;
			instance.entityID = static_cast<uint32_t>(entityID);
			instance.materialIndex = LookupMaterial(meshrender, meshIdx);
			inputs.push_back(instance);
			inputGeometry.push_back(geometryId);
		}
	}

	// ---- 2) 释放不再被引用的 BLAS ----
	for (auto it = m_BLASCache.begin(); it != m_BLASCache.end();)
	{
		if (!it->second.used)
		{
			it = m_BLASCache.erase(it);
			++result.blasReleased;
		}
		else
			++it;
	}
	result.geometryChanged = !m_IncrementalValid || result.blasBuilt > 0 || result.blasRefitted > 0 || result.blasReleased > 0;

	// refitFrom 只描述本次更新
	for (BVHBLASHint& hint : m_BLASHints)
		hint.refitFrom = 0;
	m_PatchedSlots.clear();

	// ---- 3a) 只有 refit（集合、顺序、各片段大小都没变）：原地覆盖这些片段 ----
	result.geometryPatched = m_IncrementalValid && result.blasRefitted > 0 &&
		result.blasBuilt == 0 && result.blasReleased == 0 && m_BLASOrder.size() == m_Data.blas.size();
	if (result.geometryPatched)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (uint32_t slot = 0; slot < m_BLASOrder.size(); ++slot)
		{
			const CachedBLAS& entry = m_BLASCache.at(m_BLASOrder[slot]);
			if (entry.refitFrom == 0)
				continue;
			const BVHMeshBLAS& piece = entry.piece;
			BVHBLASRange& range = m_Data.blas[slot];
			// refit 不改变节点 / 三角形数，偏移照旧
			std::copy(piece.nodes.begin(), piece.nodes.end(), m_Data.blasNodes.begin() + range.nodeOffset);
			std::copy(piece.triangles.begin(), piece.triangles.end(), m_Data.triangles.begin() + range.triangleOffset);
			std::copy(piece.attribs.begin(), piece.attribs.end(), m_Data.attribs.begin() + range.triangleOffset);
			range.bounds = piece.range.bounds;
			m_BLASKeys[slot] = entry.key;
			m_BLASHints[slot] = { entry.refitFrom, entry.dynamic };
			m_PatchedSlots.push_back(slot);
		}
	}

	// ---- 3b) BLAS 集合变了：保持已有顺序、追加新网格，重新拼接 ----
	BVHSceneData assembled;
	if (result.geometryChanged && !result.geometryPatched)
	{
		std::vector<uint64_t> order;
		order.reserve(m_BLASCache.size());
		for (uint64_t id : m_BLASOrder)
			if (m_BLASCache.count(id))
				order.push_back(id);
		{
			std::unordered_set<uint64_t> present(order.begin(), order.end());
			for (uint64_t id : newGeometry)
				if (present.insert(id).second)
					order.push_back(id);
			// 理论上不会剩：缓存条目要么在旧顺序里，要么是本次新建
			for (const auto& [id, entry] : m_BLASCache)
				if (present.insert(id).second)
					order.push_back(id);
		}
		std::vector<const BVHMeshBLAS*> pieces;
		pieces.reserve(order.size());
		m_BLASKeys.clear();
		m_BLASKeys.reserve(order.size());
		m_BLASHints.clear();
		m_BLASHints.reserve(order.size());
		for (uint64_t id : order)
		{
			const CachedBLAS& entry = m_BLASCache.at(id);
			pieces.push_back(&entry.piece);
			m_BLASKeys.push_back(entry.key);
			m_BLASHints.push_back({ entry.refitFrom, entry.dynamic });
		}
		AssembleBLAS(pieces, assembled);
		m_BLASOrder = std::move(order);
	}

	// ---- 4) 实例指向拼接后的 blas[] 下标 ----
	{
		std::unordered_map<uint64_t, uint32_t> slot;
		slot.reserve(m_BLASOrder.size());
		for (uint32_t i = 0; i < m_BLASOrder.size(); ++i)
			slot.emplace(m_BLASOrder[i], i);
		for (size_t i = 0; i < inputs.size(); ++i)
			inputs[i].meshIndex = slot.at(inputGeometry[i]);
	}

	// ---- 5) 差异：TLAS 重建 / 只修补材质 / 什么都不做 ----
	bool sameInstances = !result.geometryChanged && !m_ForceTLAS && inputs.size() == m_LastInstances.size();
	m_ForceTLAS = false;
	bool sameMaterials = true;
	for (size_t i = 0; sameInstances && i < inputs.size(); ++i)
	{
		sameInstances = SameInstanceGeometry(inputs[i], m_LastInstances[i]);
		sameMaterials = sameMaterials && inputs[i].materialIndex == m_LastInstances[i].materialIndex;
	}

	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (result.geometryChanged && !result.geometryPatched)
		{
			BuildTLAS(inputs, m_Settings, assembled);
			m_Data = std::move(assembled);
			result.instancesChanged = true;
		}
		else if (result.geometryPatched || !sameInstances)   // refit 后实例包围盒变了，TLAS 照样重建
		{
			BuildTLAS(inputs, m_Settings, m_Data);
			result.instancesChanged = true;
		}
		else if (!sameMaterials)
		{
			for (size_t i = 0; i < m_Data.instanceSource.size(); ++i)
				m_Data.instances[i].materialIndex = inputs[m_Data.instanceSource[i]].materialIndex;
			result.materialsPatched = true;
		}
		if (result.Changed())
			m_Data.stats.buildMilliseconds =
				std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
	}
	m_LastInstances = std::move(inputs);
	m_IncrementalValid = true;

	// ---- 6) 上传：BLAS 集合变化时整体重传几何 buffer；只有 refit 时只写被 refit 的区段 ----
	if (upload && result.Changed())
	{
		const auto uploadStart = std::chrono::steady_clock::now();
		bool ok = true;
		if (result.geometryPatched)
			ok = UploadGeometryPatches();
		else if (result.geometryChanged)
			ok = UploadGeometryBuffers();
		if (ok && !UploadInstanceBuffers())
		{
			ok = false;
			if (result.geometryChanged)
			{
				// 几何已换新、实例仍是旧的：偏移对不上，宁可不渲染
				m_InstanceBuffer.reset();
				m_TLASNodeBuffer.reset();
			}
		}
		m_UploadMilliseconds =
			std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uploadStart).count();
		if (ok)
			++m_Revision;
		else
		{
			// 下次被通知时重新拼接并整体上传（BLAS 缓存保留，不重建）
			result.uploadFailed = true;
			m_IncrementalValid = false;
		}
	}
	return result;
}

// ===========================================================================
// 整体重建路径
// ===========================================================================

void BVHBuilder::GatherScene(const Scene& scene)
{
	auto snapshot = std::make_shared<SceneSnapshot>();

	// Mesh 地址在本次调用期间稳定（主线程独占 Model），足够用来去重。
	std::unordered_map<const Mesh*, uint32_t> meshLookup;

	for (auto [entityID, transform, meshrender] :
		scene.m_Registry.view<const Component::Transform, const Component::MeshRender>().each())
	{
		if (!scene.IsActive(entityID) || meshrender.ModelPath.empty())
			continue;

		const Ref<Model> model = My_map::GetModel(meshrender.ModelPath);
		if (!model || model->meshes.empty())
			continue;

		const glm::mat4 objectToWorld = transform.GetTransform();

		for (size_t meshIdx = 0; meshIdx < model->meshes.size(); ++meshIdx)
		{
			const Mesh& mesh = model->meshes[meshIdx];
			if (!UsableMesh(mesh))
				continue;

			auto [it, inserted] = meshLookup.try_emplace(&mesh, static_cast<uint32_t>(snapshot->meshes.size()));
			if (inserted)
				snapshot->meshes.push_back(CopyMeshInput(mesh));

			BVHInstanceInput instance;
			instance.meshIndex = it->second;
			instance.objectToWorld = objectToWorld;
			instance.entityID = static_cast<uint32_t>(entityID);
			instance.materialIndex = LookupMaterial(meshrender, meshIdx);
			snapshot->instances.push_back(instance);
		}
	}

	std::lock_guard<std::mutex> lock(m_Mutex);
	m_Snapshot = std::move(snapshot);
}

void BVHBuilder::BuildBVH(unsigned int maxLeafSize)
{
	std::shared_ptr<const SceneSnapshot> snapshot;
	BVHBuildSettings settings;
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		snapshot = m_Snapshot;
		settings = m_Settings;
	}
	settings.maxLeafSize = maxLeafSize;

	static const SceneSnapshot kEmpty;
	const SceneSnapshot& input = snapshot ? *snapshot : kEmpty;

	// 不持锁构建：大场景要几十到几百毫秒。
	const auto start = std::chrono::steady_clock::now();
	BVHSceneData data;
	BuildSceneBVH(input.meshes, input.instances, settings, data);
	data.stats.buildMilliseconds =
		std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();

	std::lock_guard<std::mutex> lock(m_Mutex);
	m_Data = std::move(data);
	// m_Data 已不是增量路径的结果
	m_IncrementalValid = false;
}

bool BVHBuilder::UploadGeometryBuffers()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	const bool trees = !m_Data.tlasNodes.empty();
	Ref<RHIBuffer> blasNodes, triangles, attribs;
	try
	{
		if (trees)
			blasNodes = CreateStorageBuffer(m_Data.blasNodes);
		triangles = CreateStorageBuffer(m_Data.triangles);
		attribs = CreateStorageBuffer(m_Data.attribs);
	}
	catch (const std::exception& e)
	{
		Error_Core("[BVH] geometry upload failed: {}", e.what());
		return false;
	}
	if ((trees && !blasNodes) || !triangles || !attribs)
	{
		Error_Core("[BVH] geometry upload failed: a buffer is empty or exceeds 4 GB");
		return false;
	}
	// 旧 buffer 随 Ref 释放（Vulkan 走延迟删除队列）。
	m_BLASNodeBuffer = std::move(blasNodes);
	m_TriangleBuffer = std::move(triangles);
	m_TriangleAttribBuffer = std::move(attribs);
	return true;
}

bool BVHBuilder::UploadGeometryPatches()
{
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		const bool trees = !m_Data.tlasNodes.empty();
		const bool buffersMatch = m_TriangleBuffer && m_TriangleAttribBuffer && (!trees || m_BLASNodeBuffer) &&
			m_TriangleBuffer->GetSize() == m_Data.triangles.size() * sizeof(GPUBVHTriangle) &&
			m_TriangleAttribBuffer->GetSize() == m_Data.attribs.size() * sizeof(GPUBVHTriangleAttrib) &&
			(!trees || m_BLASNodeBuffer->GetSize() == m_Data.blasNodes.size() * sizeof(GPUBVHNode));
		if (buffersMatch)
		{
			try
			{
				// 原地写入；后端负责与在途帧的读取同步（Vulkan Upload 内有屏障，GL BufferSubData 隐式同步）
				for (const uint32_t slot : m_PatchedSlots)
				{
					const BVHBLASRange& r = m_Data.blas[slot];
					if (r.triangleCount == 0)
						continue;
					if (trees && r.nodeCount > 0)
						m_BLASNodeBuffer->Upload(m_Data.blasNodes.data() + r.nodeOffset,
							r.nodeCount * uint32_t(sizeof(GPUBVHNode)), r.nodeOffset * uint32_t(sizeof(GPUBVHNode)));
					m_TriangleBuffer->Upload(m_Data.triangles.data() + r.triangleOffset,
						r.triangleCount * uint32_t(sizeof(GPUBVHTriangle)), r.triangleOffset * uint32_t(sizeof(GPUBVHTriangle)));
					m_TriangleAttribBuffer->Upload(m_Data.attribs.data() + r.triangleOffset,
						r.triangleCount * uint32_t(sizeof(GPUBVHTriangleAttrib)), r.triangleOffset * uint32_t(sizeof(GPUBVHTriangleAttrib)));
				}
				return true;
			}
			catch (const std::exception& e)
			{
				Error_Core("[BVH] partial geometry upload failed, re-uploading everything: {}", e.what());
			}
		}
	}
	// 还没上传过 / 尺寸对不上 / 局部写入失败：整体重传
	return UploadGeometryBuffers();
}

bool BVHBuilder::UploadInstanceBuffers()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	const bool trees = !m_Data.tlasNodes.empty();
	Ref<RHIBuffer> tlasNodes, instances;
	try
	{
		if (trees)
			tlasNodes = CreateStorageBuffer(m_Data.tlasNodes);
		instances = CreateStorageBuffer(m_Data.instances);
	}
	catch (const std::exception& e)
	{
		Error_Core("[BVH] instance upload failed: {}", e.what());
		return false;
	}
	if ((trees && !tlasNodes) || !instances)
	{
		Error_Core("[BVH] instance upload failed: a buffer is empty or exceeds 4 GB");
		return false;
	}
	m_TLASNodeBuffer = std::move(tlasNodes);
	m_InstanceBuffer = std::move(instances);
	return true;
}

bool BVHBuilder::UploadToGPU()
{
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_Data.triangles.empty() || m_Data.instances.empty())
		{
			Warn_Core("[BVH] UploadToGPU called before BuildBVH; nothing to upload");
			return false;
		}
	}

	const auto uploadStart = std::chrono::steady_clock::now();
	// 几何先上传；实例失败时几何 buffer 已换新而实例仍旧 —— 偏移可能对不上，
	// 所以失败时把实例 buffer 也清掉，IsGPUReady() 变 false，调用方不会用到错配的数据。
	if (!UploadGeometryBuffers())
		return false;
	if (!UploadInstanceBuffers())
	{
		m_InstanceBuffer.reset();
		m_TLASNodeBuffer.reset();
		return false;
	}
	m_UploadMilliseconds =
		std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uploadStart).count();
	++m_Revision;

	const BVHStats& s = m_Data.stats;
	if (m_Data.tlasNodes.empty())
	{
		Info_Core("[BVH] flat geometry: {} instances, {} meshes, {} tris | CPU {:.1f} ms, upload {:.1f} ms",
			s.instanceCount, s.meshCount, s.triangleCount, s.buildMilliseconds, m_UploadMilliseconds);
		return true;
	}
	Info_Core("[BVH] {} instances, {} meshes, {} tris | TLAS {} nodes (depth {}) | BLAS {} nodes (depth {}) | {:.1f} ms",
		s.instanceCount, s.meshCount, s.triangleCount,
		s.tlasNodeCount, s.tlasDepth, s.blasNodeCount, s.blasDepth, s.buildMilliseconds);
	if (s.skippedTriangles || s.skippedInstances)
		Warn_Core("[BVH] skipped {} invalid triangles, {} instances (empty mesh / singular transform)",
			s.skippedTriangles, s.skippedInstances);
	return true;
}

bool BVHBuilder::Update(const Scene& scene)
{
	if (!IsDirty())
		return false;

	// 先清标记：构建过程中再发生的修改会重新标脏，下一帧接着重建。
	MarkClean();
	GatherScene(scene);
	BuildBVH(m_Settings.maxLeafSize);
	// 上传失败不重新标脏：否则每帧都会重建并刷错误日志，等下一次场景变化再试。
	return UploadToGPU();
}
