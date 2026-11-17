#include "BVHBuilder.h"

#include <chrono>
#include <exception>
#include <string>

BVHBuilder* BVHBuilder::s_ActiveInstance = nullptr;

BVHBuilder::~BVHBuilder()
{
	if (s_ActiveInstance == this)
		s_ActiveInstance = nullptr;
}

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
			continue; // 还在加载：CompleteModelLoad 会重新标脏

		const glm::mat4 objectToWorld = transform.GetTransform();

		for (size_t meshIdx = 0; meshIdx < model->meshes.size(); ++meshIdx)
		{
			const Mesh& mesh = model->meshes[meshIdx];
			if (!mesh.Active || mesh.vertices.empty() || mesh.indices.size() < 3)
				continue;

			auto [it, inserted] = meshLookup.try_emplace(&mesh, static_cast<uint32_t>(snapshot->meshes.size()));
			if (inserted)
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
				snapshot->meshes.push_back(std::move(input));
			}

			BVHInstanceInput instance;
			instance.meshIndex = it->second;
			instance.objectToWorld = objectToWorld;
			instance.entityID = static_cast<uint32_t>(entityID);
			instance.materialIndex = 0;
			if (m_MaterialMap && meshIdx < meshrender.materials.size())
			{
				const auto found = m_MaterialMap->find(meshrender.materials[meshIdx].get());
				if (found != m_MaterialMap->end())
					instance.materialIndex = found->second;
			}
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
}

namespace
{
	template<typename T>
	Ref<RHIBuffer> CreateStorageBuffer(const std::vector<T>& elements)
	{
		const size_t bytes = elements.size() * sizeof(T);
		if (bytes == 0 || bytes > UINT32_MAX) // BufferDesc::size 是 uint32_t
			return nullptr;
		return RHIBuffer::Create(BufferDesc{
			static_cast<uint32_t>(bytes), BufferUsage::Storage, false, elements.data() });
	}
}

bool BVHBuilder::UploadToGPU()
{
	std::lock_guard<std::mutex> lock(m_Mutex);

	if (m_Data.triangles.empty() || m_Data.instances.empty())
	{
		Warn_Core("[BVH] UploadToGPU called before BuildBVH; nothing to upload");
		return false;
	}

	// 平铺模式（硬件光追）没有树节点，只上传表面重建需要的 3 个 buffer。
	const bool trees = !m_Data.tlasNodes.empty();
	const auto uploadStart = std::chrono::steady_clock::now();
	Ref<RHIBuffer> tlasNodes, instances, blasNodes, triangles, attribs;
	try
	{
		if (trees)
		{
			tlasNodes = CreateStorageBuffer(m_Data.tlasNodes);
			blasNodes = CreateStorageBuffer(m_Data.blasNodes);
		}
		instances = CreateStorageBuffer(m_Data.instances);
		triangles = CreateStorageBuffer(m_Data.triangles);
		attribs = CreateStorageBuffer(m_Data.attribs);
	}
	catch (const std::exception& e)
	{
		Error_Core("[BVH] GPU upload failed: {}", e.what());
		return false;
	}

	if ((trees && (!tlasNodes || !blasNodes)) || !instances || !triangles || !attribs)
	{
		Error_Core("[BVH] GPU upload failed: a buffer is empty or exceeds 4 GB");
		return false;
	}
	m_UploadMilliseconds =
		std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uploadStart).count();
	++m_Revision;

	// 全部成功后再整体替换，保证 5 个 buffer 始终来自同一次构建。
	// 旧 buffer 随 Ref 释放（Vulkan 走延迟删除队列）。
	m_TLASNodeBuffer = std::move(tlasNodes);
	m_InstanceBuffer = std::move(instances);
	m_BLASNodeBuffer = std::move(blasNodes);
	m_TriangleBuffer = std::move(triangles);
	m_TriangleAttribBuffer = std::move(attribs);

	const BVHStats& s = m_Data.stats;
	if (!trees)
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
