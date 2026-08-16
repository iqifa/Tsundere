#pragma once
#include "Pipeline/Passes/PassCommon.h"

class PathTracePass : public RenderPass
{
public:
	bool Enabled = false;
	unsigned int MaxBounces = 4;

	void Init(Ref<FrameBuffer>& fb, Ref<RHIFramebuffer> RHIFrameBuffer = nullptr) override
	{
		m_Spec = fb->GetSpecification();

		m_Shader = GLShader::CreateCompute("D:/Code/C++/Tsundere/res/shaders/PathTrace.shader");
		if (!m_Shader || m_Shader->GetID() == 0)
		{
			Error_Core("PathTracePass: Failed to create compute shader!");
			Enabled = false;
		}

		m_AccumTex[0] = RHIStorageImage::Create({ (uint32_t)m_Spec.Width, (uint32_t)m_Spec.Height, Format::RGBA32F });
		m_AccumTex[1] = RHIStorageImage::Create({ (uint32_t)m_Spec.Width, (uint32_t)m_Spec.Height, Format::RGBA32F });

		// Initial material SSBO (RebuildMaterialBuffer fills it properly)
		GPUMaterial defMat;
		defMat.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 0.5f);
		defMat.emission = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
		defMat.diffuseHandle = 0;
		m_MaterialSSBO = StorageBuffer::Create(sizeof(GPUMaterial), &defMat, 0);
	}

	// Rebuild the GPUMaterial SSBO from the scene's CPU materials.
	// Returns the Material* → global index mapping (also stored internally).
	void RebuildMaterialBuffer(Ref<Scene> scene)
	{
		m_MatToGlobalIndex.clear();
		m_GPUMaterials.clear();

		// Collect unique materials from all MeshRender entities
		for (auto [entityID, meshrender] : scene->m_Registry.view<Component::MeshRender>().each())
		{
			for (auto& mat : meshrender.materials)
			{
				if (!mat) continue;
				if (m_MatToGlobalIndex.find(mat.get()) != m_MatToGlobalIndex.end())
					continue;

				unsigned int idx = (unsigned int)m_GPUMaterials.size();
				m_MatToGlobalIndex[mat.get()] = idx;

				GPUMaterial gpu;

				// --- debug: dump material info once per material ---
				{
					static std::set<Material*> s_Logged;
					if (s_Logged.insert(mat.get()).second)
					{
						std::string info = "PathTrace mat#" + std::to_string(idx)
							+ " shader=" + mat->shader->GetPath() + " vars[";
						for (auto& v : mat->varies)
							info += " " + std::to_string((int)std::get<1>(v)) + ":" + std::get<2>(v);
						info += " ] texID=" + std::to_string(mat->texture.GetTextureID());
						Warn_Core(info);
					}
				}

				// Try to extract base color from known uniform names
				if (!ExtractBaseColor(mat, gpu.albedo))
					gpu.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
				gpu.emission = ExtractVec4(mat, "emission",
					glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));

				// Extract diffuse texture as bindless handle
				gpu.diffuseHandle = ExtractTextureHandle(mat);

				// If no base color AND no texture, derive from material index
				if (!ExtractBaseColor(mat, gpu.albedo) && gpu.diffuseHandle == 0)
				{
					float hue = float(idx) * 0.618033988749895f;
					hue = hue - std::floor(hue);
					gpu.albedo = glm::vec4(HsvToRgb(hue, 0.6f, 0.85f), 0.5f);
					//Warn_Core("PathTrace mat#" + std::to_string(idx) + " -> HSV fallback");
				}

				m_GPUMaterials.push_back(gpu);
			}
		}

		// Ensure at least one material
		if (m_GPUMaterials.empty())
		{
			GPUMaterial defMat;
			defMat.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 0.5f);
			defMat.emission = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
			defMat.diffuseHandle = 0;
			m_GPUMaterials.push_back(defMat);
		}

		// Upload
		m_MaterialSSBO = StorageBuffer::Create(
			m_GPUMaterials.size() * sizeof(GPUMaterial),
			m_GPUMaterials.data(), 0);
	}

	void Execute(Ref<Scene> scene, RenderResources& resources) override
	{
		if (!Enabled)
		{
			resources.SceneColorTexture = (unsigned int)m_AccumTex[m_CurrentIdx]->GetNativeID();
			return;
		}

		// Rebuild material SSBO every frame (cheap, picks up UI uniform changes)
		RebuildMaterialBuffer(scene);

		// Rebuild BVH only when scene geometry changed (dirty-flag).
		// Pass material map so triangles get global material indices.
		if (m_BVHBuilder && m_BVHBuilder->IsDirty())
		{
			m_BVHBuilder->SetMaterialMap(&m_MatToGlobalIndex);
			m_BVHBuilder->GatherTriangles(scene);
			m_BVHBuilder->BuildBVH(4);
			m_BVHBuilder->UploadToGPU();
			m_BVHBuilder->MarkClean();
			m_BVHBuilder->SetMaterialMap(nullptr);
		}

		// Reset accumulation on camera movement or viewport change
		vec3 camPos = currentcamera->getpos();
		if (glm::distance(camPos, m_LastCamPos) > 0.01f ||
			m_LastViewportSize.x != (float)m_Spec.Width ||
			m_LastViewportSize.y != (float)m_Spec.Height)
		{
			m_SampleCount = 0;
			m_LastCamPos = camPos;
			m_LastViewportSize = glm::vec2((float)m_Spec.Width, (float)m_Spec.Height);
		}

		m_Shader->Bind();
		if (m_Shader->GetID() == 0)
		{
			resources.SceneColorTexture = (unsigned int)m_AccumTex[m_CurrentIdx]->GetNativeID();
			return;
		}

		// Accumulation ping-pong
		if (m_SampleCount > 0)
			m_AccumTex[m_CurrentIdx]->BindAsImage(0, ImageAccess::ReadOnly);
		m_AccumTex[1 - m_CurrentIdx]->BindAsImage(1, ImageAccess::WriteOnly);

		// BVH SSBOs
		if (!m_BVHBuilder || !m_BVHBuilder->GetTriangleBuffer() || !m_BVHBuilder->GetBVHNodeBuffer())
		{
			resources.SceneColorTexture = (unsigned int)m_AccumTex[m_CurrentIdx]->GetNativeID();
			return;
		}
		m_BVHBuilder->GetTriangleBuffer()->BindToSlot(3);
		m_BVHBuilder->GetBVHNodeBuffer()->BindToSlot(4);

		// Material SSBO
		m_MaterialSSBO->BindToSlot(5);

		// Skybox cubemap
		if (currentcamera && currentcamera->skybox && currentcamera->skybox->m_Cmp)
		{
			glActiveTexture(GL_TEXTURE6);
			glBindTexture(GL_TEXTURE_CUBE_MAP, currentcamera->skybox->m_Cmp->GetMap());
			m_Shader->SetUniform1i("u_SkyBox", 6);
		}

		// Camera uniforms
		mat4 view = currentcamera->GetViewFront();
		mat4 proj = currentcamera->GetProj();
		m_Shader->SetUniformMat4f("u_InvView", glm::inverse(view));
		m_Shader->SetUniformMat4f("u_InvProj", glm::inverse(proj));
		m_Shader->SetUniformVec3("u_CameraPos", camPos);
		m_Shader->SetUniformVec2("u_Resolution", glm::vec2((float)m_Spec.Width, (float)m_Spec.Height));
		m_Shader->SetUniform1f("u_SampleIndex", (float)m_SampleCount);
		m_Shader->SetUniform1f("u_FrameSeed", (float)m_FrameIdx);

		if (m_SampleCount > 0)
			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

		unsigned int gx = (m_Spec.Width + 7) / 8;
		unsigned int gy = (m_Spec.Height + 7) / 8;
		m_Shader->DispatchCompute(gx, gy);

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

		// Unbind image units so ImGui can safely sample the texture
		glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
		glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);

		// Swap ping-pong
		m_CurrentIdx = 1 - m_CurrentIdx;
		m_SampleCount++;
		m_FrameIdx++;

		// Output to display
		resources.SceneColorTexture = (unsigned int)m_AccumTex[m_CurrentIdx]->GetNativeID();
	}

	void OnResize(unsigned int w, unsigned int h)
	{
		m_Spec.Width = w;
		m_Spec.Height = h;
		if (m_AccumTex[0]) m_AccumTex[0]->Resize(w, h);
		if (m_AccumTex[1]) m_AccumTex[1]->Resize(w, h);
		m_SampleCount = 0;
	}

	void SetBVHBuilder(Ref<BVHBuilder> builder)
	{
		m_BVHBuilder = builder;
		// Force BVH rebuild on first frame: ShadowPass built the BVH without
		// material mapping, so triangle matIdx values are mesh-local (wrong).
		if (m_BVHBuilder)
			m_BVHBuilder->MarkDirty();
	}
	void ResetAccumulation() { m_SampleCount = 0; }
	unsigned int GetSampleCount() const { return m_SampleCount; }

private:
	// Extract uniform value from Material::varies by name
	bool HasUniform(Ref<Material> mat, const std::string& name) const
	{
		for (auto& var : mat->varies)
			if (std::get<2>(var) == name)
				return true;
		return false;
	}

	float ExtractFloat(Ref<Material> mat, const std::string& name, float defVal) const
	{
		for (auto& var : mat->varies)
		{
			if (std::get<2>(var) == name)
			{
				if (std::get<1>(var) == ValueType::FLOAT)
					return *(float*)std::get<0>(var);
				if (std::get<1>(var) == ValueType::DOUBLE)
					return (float)*(double*)std::get<0>(var);
				if (std::get<1>(var) == ValueType::INT)
					return (float)*(int*)std::get<0>(var);
			}
		}
		return defVal;
	}

	glm::vec3 ExtractVec3(Ref<Material> mat, const std::string& name, glm::vec3 defVal) const
	{
		for (auto& var : mat->varies)
			if (std::get<2>(var) == name && std::get<1>(var) == ValueType::VEC3)
				return *(glm::vec3*)std::get<0>(var);
		return defVal;
	}

	glm::vec4 ExtractVec4(Ref<Material> mat, const std::string& name, glm::vec4 defVal) const
	{
		for (auto& var : mat->varies)
			if (std::get<2>(var) == name && std::get<1>(var) == ValueType::VEC3)
				return glm::vec4(*(glm::vec3*)std::get<0>(var), defVal.a);
		return defVal;
	}

	// Try to extract base color from known uniform names.
	// Returns false if nothing found → caller uses fallback.
	bool ExtractBaseColor(Ref<Material> mat, glm::vec4& outColor) const
	{
		static const char* kNames[] = { "albedo", "color", "baseColor", "diffuseColor", "tint", "diffuse" };
		for (auto name : kNames)
		{
			for (auto& var : mat->varies)
			{
				if (std::get<2>(var) != name) continue;
				if (std::get<1>(var) == ValueType::VEC3)
					{ outColor = glm::vec4(*(glm::vec3*)std::get<0>(var), 0.5f); return true; }
				if (std::get<1>(var) == ValueType::VEC2)
					{ glm::vec2 v = *(glm::vec2*)std::get<0>(var); outColor = glm::vec4(v, 0.0f, 0.5f); return true; }
				if (std::get<1>(var) == ValueType::FLOAT)
					{ float v = *(float*)std::get<0>(var); outColor = glm::vec4(v, v, v, 0.5f); return true; }
			}
		}
		return false;
	}

	// Extract diffuse texture as bindless handle (ARB_bindless_texture).
	// Returns 0 if no valid texture found → shader falls back to albedo color.  
	GLuint64 ExtractTextureHandle(Ref<Material> mat) const
	{
		static bool s_Checked = false, s_HasBindless = false;
		if (!s_Checked)
		{
			s_HasBindless = GLEW_ARB_bindless_texture != GL_FALSE;
			s_Checked = true;
			if (!s_HasBindless)
				Warn_Core("PathTrace: GL_ARB_bindless_texture not available");
		}
		if (!s_HasBindless)
			return 0;

		// 1) Try the material's direct texture member — only if loaded (default Texture
		//    has uninitialized m_RendererID, so check GetPath() first to skip garbage)
		unsigned int texID = 0;
		if (!mat->texture.GetPath().empty())
			texID = mat->texture.GetTextureID();
		if (texID == 0)
		{
			// 2) Try the first "texture_diffuse1" in varies
			for (auto& var : mat->varies)
			{
				if (std::get<1>(var) == ValueType::TEXTURE &&
					std::get<2>(var).find("diffuse") != std::string::npos)
				{
					Texture* t = (Texture*)std::get<0>(var);
					texID = t->GetTextureID();
					break;
				}
			}
		}
		// 3) Fallback: any texture in varies
		if (texID == 0)
		{
			for (auto& var : mat->varies)
			{
				if (std::get<1>(var) == ValueType::TEXTURE)
				{
					Texture* t = (Texture*)std::get<0>(var);
					texID = t->GetTextureID();
					break;
				}
			}
		}

		if (texID == 0)
			return 0;

		// Obtain bindless handle and make resident
		GLuint64 handle = glGetTextureHandleARB(texID);
		if (handle == 0)
			return 0;

		glMakeTextureHandleResidentARB(handle);
		return handle;
	}

	static glm::vec3 HsvToRgb(float h, float s, float v)
	{
		float c = v * s;
		float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
		float m = v - c;
		glm::vec3 rgb;
		if (h < 1.0f / 6.0f)      rgb = glm::vec3(c, x, 0.0f);
		else if (h < 2.0f / 6.0f) rgb = glm::vec3(x, c, 0.0f);
		else if (h < 3.0f / 6.0f) rgb = glm::vec3(0.0f, c, x);
		else if (h < 4.0f / 6.0f) rgb = glm::vec3(0.0f, x, c);
		else if (h < 5.0f / 6.0f) rgb = glm::vec3(x, 0.0f, c);
		else                       rgb = glm::vec3(c, 0.0f, x);
		return rgb + glm::vec3(m);
	}

	FrameBufferSpecification m_Spec;
	Ref<GLShader> m_Shader;
	Ref<RHIStorageImage> m_AccumTex[2];
	int m_CurrentIdx = 0;
	Ref<BVHBuilder> m_BVHBuilder;
	Ref<StorageBuffer> m_MaterialSSBO;

	std::vector<GPUMaterial> m_GPUMaterials;
	std::unordered_map<Material*, unsigned int> m_MatToGlobalIndex;

	unsigned int m_SampleCount = 0;
	unsigned int m_FrameIdx = 0;
	glm::vec3 m_LastCamPos = glm::vec3(FLT_MAX);
	glm::vec2 m_LastViewportSize = glm::vec2(0.0f);
};

