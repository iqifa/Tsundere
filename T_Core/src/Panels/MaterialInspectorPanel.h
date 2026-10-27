#pragma once

#include "Scene/Entity.h"
#include "Core/Material.h"
#include "Core/MaterialLibrary.h"
#include "Platform/RHI/RHIShader.h"
#include "Platform/FileDialog.h"
#include "HeadLine.h"

// Inline inspector function for MeshRender materials
// Call this from your main Inspector panel when rendering a MeshRender component
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
		Ref<Model> model = My_map::GetModel(meshRender.ModelPath);
		if (model)
		{
			size_t meshCount = model->meshes.size();
			if (meshRender.materials.size() != meshCount)
			{
				meshRender.materials.resize(meshCount);
				// Fill with default materials if null
				for (size_t i = 0; i < meshCount; ++i)
				{
					if (!meshRender.materials[i])
					{
						// Create default material
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

					// Shader selection
					ImGui::Text("Shader: %s", mat->GetShader() ? mat->GetShader()->GetName().c_str() : "None");
					ImGui::SameLine();
					if (ImGui::Button("Browse##Shader"))
					{
						std::string shaderPath = OpenFileDialog(".shader");
						if (!shaderPath.empty())
						{
							Ref<RHIShader> shader = ShaderLibiray::Get(shaderPath);
							if (shader)
								mat->SetShader(shader);
						}
					}

					ImGui::Separator();
					ImGui::Text("Parameters");

					// Dynamic parameter UI based on shader reflection
					const auto& params = mat->GetParameters();
					for (auto& [name, param] : params)
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

							// Check if this is a color parameter (heuristic: name contains "color" or "albedo")
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

							// Check if color
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
								{
									ImGui::Image(texID, ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
								}
								else
								{
									ImGui::Button("[TEX]", ImVec2(64, 64));
								}

								ImGui::SameLine();
								if (ImGui::Button("X"))
									mat->SetTexture(name, nullptr);
							}
							else
							{
								ImGui::Button("[Empty]", ImVec2(64, 64));
							}

							ImGui::SameLine();
							if (ImGui::Button("Browse##Tex"))
							{
								std::string texPath = OpenTextureFileDialog();
								if (!texPath.empty())
								{
									Ref<RHITexture2D> newTex = TextureLibiary::Get(texPath);
									if (newTex)
										mat->SetTexture(name, newTex);
								}
							}
							break;
						}
						default:
							ImGui::Text("%s: (unsupported type)", name.c_str());
							break;
						}

						ImGui::PopID();
					}

					ImGui::Separator();

					// Save/Load buttons
					if (ImGui::Button("Save Material"))
					{
						std::string savePath = SaveFileDialog(".tmat");
						if (!savePath.empty())
						{
							Engine::MaterialLibrary::Save(mat, savePath);
							LOG_INFO("MaterialInspector", "Saved material to: {0}", savePath);
						}
					}

					ImGui::SameLine();
					if (ImGui::Button("Load Material"))
					{
						std::string loadPath = OpenFileDialog(".tmat");
						if (!loadPath.empty())
						{
							Ref<Engine::Material> loadedMat = Engine::MaterialLibrary::Load(loadPath);
							if (loadedMat)
							{
								meshRender.materials[i] = loadedMat;
								LOG_INFO("MaterialInspector", "Loaded material from: {0}", loadPath);
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
