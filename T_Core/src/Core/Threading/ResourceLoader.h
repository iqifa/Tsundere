#pragma once

#include "HeadLine.h"
#include "Platform/GL/Texture.h"
#include "Platform/GL/Shader.h"
#include "Scene/Modle.h"
#include "Scene/BVHBuilder.h"
#include "Panels/MeshFilePath.h"
#include "Core/Assets/TextureAsset.h"
#include "Core/Assets/MeshAsset.h"
#include "stb_image/stb_image.h"
#include "ExternalFiles.h"

#include <deque>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <functional>
#include <filesystem>

namespace Engine {

// ---------------------------------------------------------------------------
// AsyncLoadRequest
// ---------------------------------------------------------------------------
enum class LoadType { Texture, Shader, Model, BVH };

struct AsyncLoadRequest
{
	LoadType type;
	std::string path;
	std::string shaderName;

	// ---- Phase-1 outputs (worker thread → main thread) ----

	// Texture: CPU-decoded image data (TextureAsset owns the pixel vector)
	TextureAsset cpuTexture;

	// Shader: parsed source sections
	std::string vertexSource, fragmentSource, computeSource;

	// Model: CPU-only mesh data (no GL objects, safe to create on worker thread)
	std::vector<MeshAsset> loadedMeshes;

	// BVH: target builder (BuildBVH CPU part runs on worker)
	Ref<BVHBuilder> bvhBuilder;
};

// ---------------------------------------------------------------------------
// ResourceLoader — persistent worker thread for CPU/I/O work.
// All GL calls happen on the main thread via completion callbacks.
// No shared GL context needed.
// ---------------------------------------------------------------------------
class ResourceLoader
{
public:
	static void Init();
	static void Shutdown();

	// Fast enqueue (main thread / ImGui callbacks)
	static void RequestTextureLoad(const std::string& path);
	static void RequestShaderLoad(const std::string& path, const std::string& name = "");
	static void RequestModelLoad(const std::string& path);
	static void RequestBVHBuild(Ref<BVHBuilder> builder);

	// Called once per frame, BEFORE rendering
	static void DispatchQueuedLoads();
	static void ProcessMainThreadCompletions();

	// Max meshes to upload per frame (prevents frame spikes from large models)
	static constexpr int kMaxMeshesPerFrame = 8;

private:
	static void WorkerLoop();
	static void ProcessIncrementalGPUUploads();

	// Phase-1 (worker thread, CPU/I/O only, NO GL)
	static void ExecuteTextureLoad(AsyncLoadRequest& req);
	static void ExecuteShaderParse(AsyncLoadRequest& req);
	static void ExecuteModelLoad(AsyncLoadRequest& req);
	static void ExecuteBVHBuild(AsyncLoadRequest& req);

	// Phase-2 (main thread, GL calls)
	static void CompleteTextureLoad(AsyncLoadRequest& req);
	static void CompleteShaderLoad(AsyncLoadRequest& req);
	static void CompleteModelLoad(AsyncLoadRequest& req);
	static void CompleteBVHBuild(AsyncLoadRequest& req);

	// Incremental GPU upload tracking (per-model: path → next mesh index)
	struct PendingGPUModel {
		Ref<Model> model;
		std::string path;
		size_t nextMeshIdx = 0;
	};

	static std::deque<AsyncLoadRequest> s_PendingQueue;
	static std::deque<AsyncLoadRequest> s_WorkerQueue;
	static std::deque<AsyncLoadRequest> s_CompletionQueue;
	static std::deque<PendingGPUModel> s_PendingGPUModels;

	// Paths currently being loaded (dedup — prevents per-frame re-enqueue)
	static std::unordered_set<std::string> s_LoadingSet;

	static std::mutex s_PendingMutex;
	static std::mutex s_WorkerMutex;
	static std::condition_variable s_WorkerCondition;
	static std::mutex s_CompletionMutex;

	static std::atomic<bool> s_Running;
	static std::thread s_WorkerThread;
};

// ===========================================================================
// IMPLEMENTATION
// ===========================================================================

inline std::deque<AsyncLoadRequest> ResourceLoader::s_PendingQueue;
inline std::deque<AsyncLoadRequest> ResourceLoader::s_WorkerQueue;
inline std::deque<AsyncLoadRequest> ResourceLoader::s_CompletionQueue;
inline std::deque<ResourceLoader::PendingGPUModel> ResourceLoader::s_PendingGPUModels;
inline std::unordered_set<std::string> ResourceLoader::s_LoadingSet;

inline std::mutex ResourceLoader::s_PendingMutex;
inline std::mutex ResourceLoader::s_WorkerMutex;
inline std::condition_variable ResourceLoader::s_WorkerCondition;
inline std::mutex ResourceLoader::s_CompletionMutex;

inline std::atomic<bool> ResourceLoader::s_Running{false};
inline std::thread ResourceLoader::s_WorkerThread;

// ---- Init / Shutdown ----

inline void ResourceLoader::Init()
{
	if (s_Running.load()) return;
	s_Running.store(true);
	s_WorkerThread = std::thread(WorkerLoop);
	Info_Core("ResourceLoader: Worker thread started");
}

inline void ResourceLoader::Shutdown()
{
	if (!s_Running.load()) return;
	s_Running.store(false);
	s_WorkerCondition.notify_all();
	if (s_WorkerThread.joinable())
		s_WorkerThread.join();
	Info_Core("ResourceLoader: Worker thread stopped");
}

// ---- Request methods (main thread, non-blocking) ----

inline void ResourceLoader::RequestTextureLoad(const std::string& path)
	{
		std::lock_guard lock(s_PendingMutex);
		if (s_LoadingSet.find(path) != s_LoadingSet.end())
			return;
		s_LoadingSet.insert(path);
		AsyncLoadRequest req;
		req.type = LoadType::Texture;
		req.path = path;
		s_PendingQueue.push_back(std::move(req));
	}

inline void ResourceLoader::RequestShaderLoad(const std::string& path, const std::string& name)
{
	AsyncLoadRequest req;
	req.type = LoadType::Shader;
	req.path = path;
	req.shaderName = name;
	std::lock_guard lock(s_PendingMutex);
	s_PendingQueue.push_back(std::move(req));
}

inline void ResourceLoader::RequestModelLoad(const std::string& path)
	{
		std::lock_guard lock(s_PendingMutex);
		if (s_LoadingSet.find(path) != s_LoadingSet.end())
			return;
		s_LoadingSet.insert(path);
		AsyncLoadRequest req;
		req.type = LoadType::Model;
		req.path = path;
		s_PendingQueue.push_back(std::move(req));
	}

inline void ResourceLoader::RequestBVHBuild(Ref<BVHBuilder> builder)
{
	if (!builder) return;
	AsyncLoadRequest req;
	req.type = LoadType::BVH;
	req.bvhBuilder = builder;
	std::lock_guard lock(s_PendingMutex);
	s_PendingQueue.push_back(std::move(req));
}

// ---- Dispatch + Process (main loop) ----

inline void ResourceLoader::DispatchQueuedLoads()
{
	std::deque<AsyncLoadRequest> batch;
	{
		std::lock_guard lock(s_PendingMutex);
		batch.swap(s_PendingQueue);
	}
	if (batch.empty()) return;

	{
		std::lock_guard lock(s_WorkerMutex);
		for (auto& req : batch)
			s_WorkerQueue.push_back(std::move(req));
	}
	s_WorkerCondition.notify_one();
}

inline void ResourceLoader::ProcessMainThreadCompletions()
{
	// 1. Process completed async loads (Phase-2 GPU uploads)
	std::deque<AsyncLoadRequest> completed;
	{
		std::lock_guard lock(s_CompletionMutex);
		completed.swap(s_CompletionQueue);
	}

	for (auto& req : completed)
	{
		switch (req.type)
		{
		case LoadType::Texture: CompleteTextureLoad(req); break;
		case LoadType::Shader:  CompleteShaderLoad(req);  break;
		case LoadType::Model:   CompleteModelLoad(req);   break;
		case LoadType::BVH:     CompleteBVHBuild(req);    break;
		}
	}

	// 2. Continue incremental GPU upload for large models
	ProcessIncrementalGPUUploads();
}

// ---- Worker loop ----

inline void ResourceLoader::WorkerLoop()
{
	while (s_Running.load())
	{
		AsyncLoadRequest req;
		bool hasWork = false;

		{
			std::unique_lock lock(s_WorkerMutex);
			s_WorkerCondition.wait(lock, [] {
				return !s_WorkerQueue.empty() || !s_Running.load();
			});
			if (!s_Running.load()) break;
			if (!s_WorkerQueue.empty())
			{
				req = std::move(s_WorkerQueue.front());
				s_WorkerQueue.pop_front();
				hasWork = true;
			}
		}

		if (!hasWork) continue;

		switch (req.type)
		{
		case LoadType::Texture: ExecuteTextureLoad(req);  break;
		case LoadType::Shader:  ExecuteShaderParse(req);   break;
		case LoadType::Model:   ExecuteModelLoad(req);     break;
		case LoadType::BVH:     ExecuteBVHBuild(req);      break;
		}

		{
			std::lock_guard lock(s_CompletionMutex);
			s_CompletionQueue.push_back(std::move(req));
		}
	}
}

// ===========================================================================
// Phase-1: CPU/I/O on worker thread (NO OpenGL calls)
// ===========================================================================

inline void ResourceLoader::ExecuteTextureLoad(AsyncLoadRequest& req)
{
	// Phase-1: 在 worker 线程解码图片，全部存入 std::vector（无 GL 调用）
	stbi_set_flip_vertically_on_load(1);
	int w = 0, h = 0, ch = 0;
	uint8_t* raw = stbi_load(req.path.c_str(), &w, &h, &ch, 4); // 强制 RGBA
	if (raw) {
		req.cpuTexture.path     = req.path;
		req.cpuTexture.width    = w;
		req.cpuTexture.height   = h;
		req.cpuTexture.channels = 4; // 已强制 4 通道
		req.cpuTexture.pixels.assign(raw, raw + static_cast<size_t>(w) * h * 4);
		stbi_image_free(raw);
	} else {
		Error_Core("ResourceLoader: Failed to load texture: " + req.path);
	}
}

inline void ResourceLoader::ExecuteShaderParse(AsyncLoadRequest& req)
{
	std::ifstream stream(req.path);
	if (!stream.is_open())
	{
		Error_Core("ResourceLoader: Failed to open shader: " + req.path);
		return;
	}

	enum class ShType { NONE = -1, VERTEX = 0, FRAGMENT = 1, COMPUTE = 2 };
	std::string line;
	std::stringstream ss[3];
	ShType type = ShType::NONE;

	while (std::getline(stream, line))
	{
		if (line.find("#shader") != std::string::npos)
		{
			if (line.find("vertex") != std::string::npos)
				type = ShType::VERTEX;
			else if (line.find("fragment") != std::string::npos)
				type = ShType::FRAGMENT;
			else if (line.find("compute") != std::string::npos)
				type = ShType::COMPUTE;
		}
		else
		{
			ss[(int)type] << line << '\n';
		}
	}

	req.vertexSource = ss[0].str();
	req.fragmentSource = ss[1].str();
	req.computeSource = ss[2].str();
}

inline void ResourceLoader::ExecuteModelLoad(AsyncLoadRequest& req)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(req.path,
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_FlipUVs |
		aiProcess_CalcTangentSpace |
		aiProcess_ConvertToLeftHanded |
		aiProcess_FixInfacingNormals);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::string absolutePath = std::filesystem::absolute(req.path).string();
		std::string assimpError = importer.GetErrorString();

		Error_Core("ResourceLoader: Failed to load model: " + req.path);

		Error_Core("ResourceLoader: Failed to load model: " + req.path +
			"\nAssimp Error: " + assimpError +
			"\nAttempted Absolute Path: " + absolutePath);
		return;
	}

	std::function<void(aiNode*, const aiScene*)> processNode =
		[&](aiNode* node, const aiScene* scn)
	{
		for (unsigned int i = 0; i < node->mNumMeshes; i++)
		{
			aiMesh* mesh = scn->mMeshes[node->mMeshes[i]];
			std::vector<Vertex> vertices;
			std::vector<unsigned int> indices;

			for (unsigned int v = 0; v < mesh->mNumVertices; v++)
			{
				Vertex vert;
				vert.Position = glm::vec3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
				if (mesh->HasNormals())
					vert.Normal = glm::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
				if (mesh->mTextureCoords[0])
				{
					vert.TexCoords = glm::vec2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
					vert.Tangent = glm::vec3(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z);
					vert.Bitangent = glm::vec3(mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z);
				}
				else
					vert.TexCoords = glm::vec2(0.0f, 0.0f);
				vertices.push_back(vert);
			}

			for (unsigned int f = 0; f < mesh->mNumFaces; f++)
			{
				aiFace face = mesh->mFaces[f];
				for (unsigned int j = 0; j < face.mNumIndices; j++)
					indices.push_back(face.mIndices[j]);
			}
			// Phase-1: 只存 CPU 数据（MeshAsset），不创建任何 GL 对象
			MeshAsset cpuMesh;
			cpuMesh.vertices = std::move(vertices);
			cpuMesh.indices  = std::move(indices);
			req.loadedMeshes.push_back(std::move(cpuMesh));
		}
		for (unsigned int i = 0; i < node->mNumChildren; i++)
			processNode(node->mChildren[i], scn);
	};

	processNode(scene->mRootNode, scene);
}

inline void ResourceLoader::ExecuteBVHBuild(AsyncLoadRequest& req)
{
	// CPU-only: recursive BVH tree build (sort + AABB split).
	// GatherTriangles must have been called on the main thread before enqueuing.
	// BuildBVH fills m_GPUTriangles and m_BVHNodes (no GL calls).
	// UploadToGPU is called on the main thread in CompleteBVHBuild.
	if (req.bvhBuilder)
	{
		req.bvhBuilder->BuildBVH(4);
	}
}

// ===========================================================================
// Phase-2: GPU upload on main thread
// ===========================================================================

inline void ResourceLoader::CompleteTextureLoad(AsyncLoadRequest& req)
{
	{
		std::lock_guard lock(s_PendingMutex);
		s_LoadingSet.erase(req.path);
	}

	if (!req.cpuTexture.IsValid()) return;

	// Phase-2: 主线程 GL 上传（CreateFromAsset 不会释放 pixels，生命周期由 cpuTexture 管理）
	auto tex = Texture::CreateFromAsset(req.cpuTexture);

	// GPU 上传完成后，释放 CPU 像素内存（除非设置了 keepCPUCopy）
	if (!req.cpuTexture.keepCPUCopy) {
		req.cpuTexture.pixels.clear();
		req.cpuTexture.pixels.shrink_to_fit();
	}

	if (tex) {
		std::unique_lock lock(TextureLibiary::s_Mutex);
		TextureLibiary::m_TextureMap[req.path] = tex;
	}

	Info_Core("Texture loaded: " + req.path);
}

inline void ResourceLoader::CompleteShaderLoad(AsyncLoadRequest& req)
{
	// Shader files are small — re-read + compile + link on main thread.
	// The main win of async is texture/model I/O (stbi/Assimp).
	// ShaderLibiray::Load handles everything correctly.
	if (req.shaderName.empty())
		ShaderLibiray::Load(req.path);
	else
		ShaderLibiray::Load(req.shaderName, req.path);

	Info_Core("Shader loaded: " + req.path);
}

inline void ResourceLoader::CompleteModelLoad(AsyncLoadRequest& req)
{
	{
		std::lock_guard lock(s_PendingMutex);
		s_LoadingSet.erase(req.path);
	}

	if (req.loadedMeshes.empty()) return;

	Ref<Model> model = CreateRef<Model>();
	// Convert vector<MeshAsset> → vector<Mesh> (CPU only, no GL yet)
	model->meshes.reserve(req.loadedMeshes.size());
	for (auto& asset : req.loadedMeshes)
		model->meshes.push_back(Mesh::CreateFromAsset(std::move(asset)));

	{
		std::unique_lock lock(My_map::s_Mutex);
		My_map::m_ModleMap[req.path] = model;
	}

	PendingGPUModel pending;
	pending.model = model;
	pending.path = req.path;
	pending.nextMeshIdx = 0;
	s_PendingGPUModels.push_back(std::move(pending));

	Info_Core("Model data loaded (GPU upload pending): " + req.path
		+ " (" + std::to_string(model->meshes.size()) + " meshes)");
}

// Process a limited number of pending mesh GPU uploads each frame.
// Called after completion processing, before rendering.
inline void ResourceLoader::ProcessIncrementalGPUUploads()
{
	int uploaded = 0;

	while (!s_PendingGPUModels.empty() && uploaded < kMaxMeshesPerFrame)
	{
		auto& pending = s_PendingGPUModels.front();
		auto& meshes = pending.model->meshes;

		while (pending.nextMeshIdx < meshes.size() && uploaded < kMaxMeshesPerFrame)
		{
			if (!meshes[pending.nextMeshIdx].IsGPUReady())
			{
				meshes[pending.nextMeshIdx].setupMesh();
				uploaded++;
			}
			pending.nextMeshIdx++;
		}

		if (pending.nextMeshIdx >= meshes.size())
		{
			Info_Core("Model GPU upload complete: " + pending.path
				+ " (" + std::to_string(meshes.size()) + " meshes)");
			s_PendingGPUModels.pop_front();
		}
		else
		{
			// More meshes remain — continue next frame
			break;
		}
	}
}

inline void ResourceLoader::CompleteBVHBuild(AsyncLoadRequest& req)
{
	// GPU upload on main thread (glBufferData for SSBOs)
	if (req.bvhBuilder)
	{
		req.bvhBuilder->UploadToGPU();
		req.bvhBuilder->MarkClean();
	}
}

} // namespace Engine
