#pragma once

#include "Scene/Entity.h"
#include "Core/Material.h"
#include "Core/MaterialLibrary.h"
#include "Platform/RHI/RHIShader.h"
#include "Platform/RHI/RHITextureLibrary.h"
#include "Platform/RHI/RHIImGuiRenderer.h"
#include "Platform/FileDialog.h"
#include "Core/Assets/AssetDatabase.h"
#include "Panels/MeshFilePath.h"
#include "Debug/Debug.h"
#include "HeadLine.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <unordered_set>

// Writes the material back to its .tmat when it already has an asset path.
inline void SaveMaterialAsset(const Ref<Engine::Material>& mat)
{
	if (!mat || mat->GetAssetPath().empty())
		return;
	Engine::MaterialLibrary::Save(mat, mat->GetAssetPath());
}

// Parameter UI for one material. Driven by shader reflection: each uniform
// becomes an editor widget of the reflected type.
inline void DrawMaterialFields(Ref<Engine::Material> mat)
{
	if (!mat)
		return;

	char nameBuf[128];
	strcpy_s(nameBuf, sizeof(nameBuf), mat->GetName().c_str());
	if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
		mat->SetName(nameBuf);
	if (ImGui::IsItemDeactivatedAfterEdit())
		SaveMaterialAsset(mat);

	// Deferred mutations: SetShader() rebuilds m_Parameters, which would
	// invalidate references into it below. Apply only after all UI is done.
	std::string pendingShaderPath;

	if (!mat->GetAssetPath().empty())
		ImGui::TextDisabled("Asset: %s", mat->GetAssetPath().c_str());

	{
		std::string shaderPath = mat->GetShader() ? mat->GetShader()->GetPath() : "";
		std::string shaderLabel = shaderPath.empty()
			? "<none>"
			: shaderPath.substr(shaderPath.find_last_of("/\\") + 1);
		if (ImGui::BeginCombo("Shader", shaderLabel.c_str()))
		{
			std::unordered_set<std::string> seen;
			auto addShader = [&](const std::string& path, const char* id)
			{
				const std::string key = Engine::AssetDatabase::NormalizePath(path);
				if (!seen.insert(key).second)
					return;
				ImGui::PushID(id);
				const bool selected = !shaderPath.empty()
					&& key == Engine::AssetDatabase::NormalizePath(shaderPath);
				const std::string display = path.substr(path.find_last_of("/\\") + 1);
				if (ImGui::Selectable(display.c_str(), selected))
				{
					// Defer: SetShader() rebuilds m_Parameters mid-iteration.
					pendingShaderPath = path;
					SaveMaterialAsset(mat);
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
				ImGui::PopID();
			};

			for (const auto& record : Engine::AssetDatabase::GetByKind(Engine::AssetKind::Shader))
			{
				char uidLabel[32];
				std::snprintf(uidLabel, sizeof(uidLabel), "%llu",
					static_cast<unsigned long long>(record.uid));
				addShader(record.path, uidLabel);
			}
			for (const auto& path : My_map::GetShaderPaths())
				addShader(path, path.c_str());
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button("...##ShaderBrowse"))
		{
			std::string picked = OpenFileDialog(
				"Shader Files\0*.shader;*.glsl;*.vert;*.frag\0All Files\0*.*\0");
			if (!picked.empty())
			{
				// Defer: SetShader() rebuilds m_Parameters mid-iteration.
				pendingShaderPath = picked;
				SaveMaterialAsset(mat);
			}
		}
	}

	ImGui::Separator();
	ImGui::Text("Parameters");

	for (auto& [name, param] : mat->GetParameters())
	{
		ImGui::PushID(name.c_str());

		switch (param.type)
		{
		case Engine::MaterialParameter::Type::Float:
		{
			float val = mat->GetFloat(name);
			if (ImGui::DragFloat(name.c_str(), &val, 0.01f))
				mat->SetFloat(name, val);
			if (ImGui::IsItemDeactivatedAfterEdit())
				SaveMaterialAsset(mat);
			break;
		}
		case Engine::MaterialParameter::Type::Vec2:
		{
			glm::vec2 val = mat->GetVec2(name);
			if (ImGui::DragFloat2(name.c_str(), &val.x, 0.01f))
				mat->SetVec2(name, val);
			if (ImGui::IsItemDeactivatedAfterEdit())
				SaveMaterialAsset(mat);
			break;
		}
		case Engine::MaterialParameter::Type::Vec3:
		{
			glm::vec3 val = mat->GetVec3(name);

			std::string lowerName = name;
			std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
			bool isColor = (lowerName.find("color") != std::string::npos ||
				lowerName.find("albedo") != std::string::npos ||
				lowerName.find("diffuse") != std::string::npos);

			if (isColor)
			{
				if (ImGui::ColorEdit3(name.c_str(), &val.x))
					mat->SetVec3(name, val);
			}
			else
			{
				if (ImGui::DragFloat3(name.c_str(), &val.x, 0.01f))
					mat->SetVec3(name, val);
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
				SaveMaterialAsset(mat);
			break;
		}
		case Engine::MaterialParameter::Type::Vec4:
		{
			glm::vec4 val = mat->GetVec4(name);

			std::string lowerName = name;
			std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
			bool isColor = (lowerName.find("color") != std::string::npos ||
				lowerName.find("albedo") != std::string::npos);

			if (isColor)
			{
				if (ImGui::ColorEdit4(name.c_str(), &val.x))
					mat->SetVec4(name, val);
			}
			else
			{
				if (ImGui::DragFloat4(name.c_str(), &val.x, 0.01f))
					mat->SetVec4(name, val);
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
				SaveMaterialAsset(mat);
			break;
		}
		case Engine::MaterialParameter::Type::Int:
		{
			int val = mat->GetInt(name);
			if (ImGui::DragInt(name.c_str(), &val))
				mat->SetInt(name, val);
			if (ImGui::IsItemDeactivatedAfterEdit())
				SaveMaterialAsset(mat);
			break;
		}
		case Engine::MaterialParameter::Type::Texture2D:
		{
			Ref<RHITexture2D> tex = mat->GetTexture(name);

			ImGui::Text("%s:", name.c_str());

			if (tex)
			{
				ImTextureID texID = RHIImGUIRenderer::GetTextureID(tex.get());
				if (texID != ImTextureID_Invalid)
					ImGui::Image(texID, ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
				else
					ImGui::Button("[TEX]", ImVec2(64, 64));
			}
			else
			{
				ImGui::Button("[Empty]", ImVec2(64, 64));
			}

			ImGui::SameLine();
			ImGui::BeginGroup();
			if (ImGui::BeginCombo("##TexCombo", tex ? "(assigned)" : "(none)"))
			{
				std::unordered_set<std::string> seen;
				auto addTexture = [&](const std::string& path, const char* id, Ref<RHITexture2D> loaded)
				{
					const std::string key = Engine::AssetDatabase::NormalizePath(path);
					if (!seen.insert(key).second)
						return;
					ImGui::PushID(id);
					const bool selected = tex
						&& key == Engine::AssetDatabase::NormalizePath(tex->GetPath());
					const std::string display = path.substr(path.find_last_of("/\\") + 1);
					if (ImGui::Selectable(display.c_str(), selected))
					{
						Ref<RHITexture2D> assigned = loaded ? loaded : TextureLibiary::Get(path);
						if (assigned)
						{
							mat->SetTexture(name, assigned);
							SaveMaterialAsset(mat);
						}
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
					ImGui::PopID();
				};

				for (const auto& record : Engine::AssetDatabase::GetByKind(Engine::AssetKind::Texture))
				{
					char uidLabel[32];
					std::snprintf(uidLabel, sizeof(uidLabel), "%llu",
						static_cast<unsigned long long>(record.uid));
					addTexture(record.path, uidLabel, nullptr);
				}
				for (auto& [path, texture] : TextureLibiary::Entries())
					addTexture(path, path.c_str(), texture);
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::Button("Browse"))
			{
				std::string texPath = OpenTextureFileDialog();
				if (!texPath.empty())
				{
					Ref<RHITexture2D> loadedTex = TextureLibiary::Load(texPath);
					if (loadedTex)
					{
						mat->SetTexture(name, loadedTex);
						SaveMaterialAsset(mat);
					}
				}
			}
			if (tex)
			{
				ImGui::SameLine();
				if (ImGui::Button("X"))
				{
					mat->SetTexture(name, nullptr);
					SaveMaterialAsset(mat);
				}
			}
			ImGui::EndGroup();
			break;
		}
		default:
			ImGui::Text("%s: (unsupported type)", name.c_str());
			break;
		}

		ImGui::PopID();
	}

	// Apply deferred shader switch after the parameter loop is done.
	if (!pendingShaderPath.empty())
		mat->SetShader(ShaderLibiray::Get(pendingShaderPath));
}

// Inline inspector for MeshRender materials.
// A material that already has a .tmat path is written back as its fields change.
inline void RenderMaterialInspector(Entity& entity)
{
	if (!entity.HasComponent<Component::MeshRender>())
		return;

	auto& meshRender = entity.GetComponent<Component::MeshRender>();

	ImGui::PushID("##MaterialInspector");

	bool open = ImGui::CollapsingHeader("Materials",
		ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);

	if (open)
	{
		ImGui::Indent();

		// Ensure materials vector matches model mesh count
		Ref<Model> model = meshRender.ModelPath.empty() ? nullptr : My_map::GetModel(meshRender.ModelPath);
		if (model)
		{
			size_t meshCount = model->meshes.size();
			if (meshRender.materials.size() != meshCount)
			{
				meshRender.materials.resize(meshCount);
				for (size_t i = 0; i < meshCount; ++i)
				{
					if (!meshRender.materials[i])
					{
						Ref<RHIShader> defaultShader = ShaderLibiray::Get("res/shaders/Lamber.shader");
						meshRender.materials[i] = Engine::MaterialLibrary::CreateDefault("DefaultMaterial", defaultShader);
					}
				}
			}

			// Display each material
			for (size_t i = 0; i < meshCount; ++i)
			{
				ImGui::PushID((int)i);

				std::string header = "Mesh " + std::to_string(i);
				if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					Ref<Engine::Material> mat = meshRender.materials[i];

					{
						std::string preview = "<none>";
						if (mat)
						{
							preview = mat->GetAssetPath().empty()
								? mat->GetName()
								: mat->GetAssetPath().substr(mat->GetAssetPath().find_last_of("/\\") + 1);
						}
						if (ImGui::BeginCombo("Material", preview.c_str()))
						{
							for (const auto& record : Engine::AssetDatabase::GetByKind(Engine::AssetKind::Material))
							{
								char uidLabel[32];
								std::snprintf(uidLabel, sizeof(uidLabel), "%llu",
									static_cast<unsigned long long>(record.uid));
								ImGui::PushID(uidLabel);
								const bool selected = mat && !mat->GetAssetPath().empty()
									&& Engine::AssetDatabase::NormalizePath(mat->GetAssetPath()) == record.path;
								const std::string display = record.path.substr(record.path.find_last_of("/\\") + 1);
								if (ImGui::Selectable(display.c_str(), selected))
								{
									Ref<Engine::Material> loaded = Engine::MaterialLibrary::Get(record.path);
									if (loaded)
									{
										meshRender.materials[i] = loaded;
										mat = loaded;
									}
								}
								if (selected)
									ImGui::SetItemDefaultFocus();
								ImGui::PopID();
							}
							ImGui::EndCombo();
						}
					}

					if (!mat)
					{
						ImGui::Text("No material assigned");
						if (ImGui::Button("Create Material"))
						{
							Ref<RHIShader> defaultShader = ShaderLibiray::Get("res/shaders/Lamber.shader");
							meshRender.materials[i] = Engine::MaterialLibrary::CreateDefault("Material", defaultShader);
						}
						ImGui::TreePop();
						ImGui::PopID();
						continue;
					}

					DrawMaterialFields(mat);

					ImGui::SameLine();
					if (ImGui::Button("Load Material"))
					{
						std::string loadPath = OpenFileDialog("Tsundere Material\0*.tmat\0All Files\0*.*\0");
						if (!loadPath.empty())
						{
							Ref<Engine::Material> loadedMat = Engine::MaterialLibrary::Load(loadPath);
							if (loadedMat)
							{
								meshRender.materials[i] = loadedMat;
								Info_Core("Loaded material from: {}", loadPath);
							}
						}
					}

					ImGui::TreePop();
				}

				ImGui::PopID();
			}
		}
		else
		{
			ImGui::Text("Model not loaded: %s", meshRender.ModelPath.c_str());
		}

		ImGui::Unindent();
	}

	ImGui::PopID();
}
