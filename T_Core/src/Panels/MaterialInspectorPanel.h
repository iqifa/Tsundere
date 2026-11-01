#pragma once

#include "Scene/Entity.h"
#include "Core/Material.h"
#include "Core/MaterialLibrary.h"
#include "Platform/RHI/RHIShader.h"
#include "Platform/RHI/RHITextureLibrary.h"
#include "Platform/RHI/RHIImGuiRenderer.h"
#include "Platform/FileDialog.h"
#include "Panels/MeshFilePath.h"
#include "Debug/Debug.h"
#include "HeadLine.h"

// Inline inspector for MeshRender materials.
// Parameter UI is driven entirely by shader reflection: each uniform declared
// by the material's shader becomes an editor widget of the reflected type.
// Materials persist as .tmat files via MaterialLibrary (Save/Load).
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
						Ref<RHIShader> defaultShader = ShaderLibiray::Get("res/shaders/default.shader");
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

					if (!mat)
					{
						ImGui::Text("No material assigned");
						if (ImGui::Button("Create Material"))
						{
							Ref<RHIShader> defaultShader = ShaderLibiray::Get("res/shaders/default.shader");
							meshRender.materials[i] = Engine::MaterialLibrary::CreateDefault("Material", defaultShader);
						}
						ImGui::TreePop();
						ImGui::PopID();
						continue;
					}

					// Material name
					char nameBuf[128];
					strcpy_s(nameBuf, sizeof(nameBuf), mat->GetName().c_str());
					if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
						mat->SetName(nameBuf);

					// Asset path ("" = not yet saved)
					if (!mat->GetAssetPath().empty())
						ImGui::TextDisabled("Asset: %s", mat->GetAssetPath().c_str());

					// Shader selection by path combo
					{
						std::string shaderPath = mat->GetShader() ? mat->GetShader()->GetPath() : "";
						std::string shaderLabel = shaderPath.empty()
							? "<none>"
							: shaderPath.substr(shaderPath.find_last_of("/\\") + 1);
						if (ImGui::BeginCombo("Shader", shaderLabel.c_str()))
						{
							for (const auto& path : My_map::GetShaderPaths())
							{
								bool selected = (path == shaderPath);
								std::string display = path.substr(path.find_last_of("/\\") + 1);
								if (ImGui::Selectable(display.c_str(), selected))
									mat->SetShader(ShaderLibiray::Get(path));
								if (selected)
									ImGui::SetItemDefaultFocus();
							}
							ImGui::EndCombo();
						}
						ImGui::SameLine();
						if (ImGui::Button("...##ShaderBrowse"))
						{
							std::string picked = OpenFileDialog(
								"Shader Files\0*.shader;*.glsl;*.vert;*.frag\0All Files\0*.*\0");
							if (!picked.empty())
								mat->SetShader(ShaderLibiray::Get(picked));
						}
					}

					ImGui::Separator();
					ImGui::Text("Parameters");

					// Dynamic parameter UI based on shader reflection
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
							break;
						}
						case Engine::MaterialParameter::Type::Vec2:
						{
							glm::vec2 val = mat->GetVec2(name);
							if (ImGui::DragFloat2(name.c_str(), &val.x, 0.01f))
								mat->SetVec2(name, val);
							break;
						}
						case Engine::MaterialParameter::Type::Vec3:
						{
							glm::vec3 val = mat->GetVec3(name);

							// Color heuristic: name suggests a color parameter
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
							break;
						}
						case Engine::MaterialParameter::Type::Int:
						{
							int val = mat->GetInt(name);
							if (ImGui::DragInt(name.c_str(), &val))
								mat->SetInt(name, val);
							break;
						}
						case Engine::MaterialParameter::Type::Texture2D:
						{
							Ref<RHITexture2D> tex = mat->GetTexture(name);

							ImGui::Text("%s:", name.c_str());

							// Display texture thumbnail
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

							// Texture picker from the texture library
							ImGui::SameLine();
							ImGui::BeginGroup();
							if (ImGui::BeginCombo("##TexCombo", tex ? "(assigned)" : "(none)"))
							{
								for (auto& [path, texture] : TextureLibiary::m_TextureMap)
								{
									if (ImGui::Selectable(path.c_str(), tex == texture))
										mat->SetTexture(name, texture);
								}
								ImGui::EndCombo();
							}
							ImGui::SameLine();
							if (ImGui::Button("Browse"))
							{
								std::string texPath = OpenTextureFileDialog();
								if (!texPath.empty())
									mat->SetTexture(name, TextureLibiary::Load(texPath));
							}
							if (tex)
							{
								ImGui::SameLine();
								if (ImGui::Button("X"))
									mat->SetTexture(name, nullptr);
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

					ImGui::Separator();

					// Persistence: save / load .tmat
					if (ImGui::Button("Save Material"))
					{
						std::string savePath = mat->GetAssetPath().empty()
							? SaveFileDialog("Tsundere Material\0*.tmat\0All Files\0*.*\0")
							: mat->GetAssetPath();
						if (!savePath.empty() && Engine::MaterialLibrary::Save(mat, savePath))
							Info_Core("Saved material to: {}", savePath);
					}

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
