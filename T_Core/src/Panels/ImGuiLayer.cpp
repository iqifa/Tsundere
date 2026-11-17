#include "ImGuiLayer.h"
#include "Panels/EditorWindows.h"
#include <utility>
#include "Trans/SceneCamera.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

bool& EditorWindows::Skybox()
{
    static bool visible = false;
    return visible;
}

bool& EditorWindows::ShadowMap()
{
    static bool open = true;
    return open;
}

bool& EditorWindows::Console()
{
    static bool open = true;
    return open;
}

namespace Engine
{
    namespace
    {
        std::string preferenceStatus;

        std::filesystem::path PreferencesPath()
        {
            wchar_t executable[32768]{};
            const auto length = GetModuleFileNameW(nullptr, executable, 32768);
            if (!length || length == 32768)
                throw std::runtime_error("Cannot locate preferences directory");
            auto directory = std::filesystem::path(executable).parent_path();
            while (!directory.empty())
            {
                if (std::filesystem::is_directory(directory / "res"))
                    return directory / "ProjectSettings" / "Preferences.ini";
                const auto parent = directory.parent_path();
                if (parent == directory) break;
                directory = parent;
            }
            throw std::runtime_error("Cannot locate project preferences directory");
        }

        void LoadPreferences()
        {
            try
            {
                const auto path = PreferencesPath();
                if (!std::filesystem::exists(path)) return;
                std::ifstream file(path);
                std::string nearKey, farKey;
                float nearPlane = 0, farPlane = 0;
                if (!(file >> nearKey >> nearPlane >> farKey >> farPlane)
                    || nearKey != "near_clip" || farKey != "far_clip")
                    throw std::runtime_error("Invalid camera preferences");
                file >> std::ws;
                if (!file.eof() || !currentcamera || !currentcamera->SetClipPlanes(nearPlane, farPlane))
                    throw std::runtime_error("Invalid camera clipping range");
            }
            catch (const std::exception& e) { preferenceStatus = e.what(); }
        }

        void SavePreferences()
        {
            std::filesystem::path temporary;
            try
            {
                const auto path = PreferencesPath();
                std::filesystem::create_directories(path.parent_path());
                temporary = path.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                file << std::setprecision(std::numeric_limits<float>::max_digits10)
                     << "near_clip " << currentcamera->getNear() << '\n'
                     << "far_clip " << currentcamera->getFar() << '\n';
                file.close();
                if (!file || !MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot save preferences");
                preferenceStatus = "Preferences saved.";
            }
            catch (const std::exception& e)
            {
                preferenceStatus = e.what();
                std::error_code ignored;
                if (!temporary.empty()) std::filesystem::remove(temporary, ignored);
            }
        }

        void DrawPreferences(bool& open)
        {
            if (!open) return;
            ImGui::SetNextWindowSize(ImVec2(430, 240), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Preferences", &open) && currentcamera)
            {
                ImGui::TextUnformatted("Scene Camera");
                ImGui::Separator();
                float nearPlane = currentcamera->getNear();
                float farPlane = currentcamera->getFar();
                bool changed = ImGui::InputFloat("Near clip", &nearPlane, 0.01f, 0.1f, "%.4f");
                changed |= ImGui::InputFloat("Far clip", &farPlane, 10.0f, 100.0f, "%.2f");
                if (changed)
                    preferenceStatus = currentcamera->SetClipPlanes(nearPlane, farPlane)
                        ? "Applied. Save to keep these settings after restart."
                        : "Enter finite values with 0 < Near < Far.";
                ImGui::TextWrapped("Distances use world units. Changes apply immediately.");
                if (ImGui::Button("Restore defaults"))
                {
                    currentcamera->SetClipPlanes(SceneCamera::DefaultNear, SceneCamera::DefaultFar);
                    preferenceStatus = "Defaults applied. Save to keep them after restart.";
                }
                ImGui::SameLine();
                if (ImGui::Button("Save preferences")) SavePreferences();
                if (!preferenceStatus.empty()) ImGui::TextWrapped("%s", preferenceStatus.c_str());
            }
            ImGui::End();
        }
    }

    static void ShowDockSpace(const std::function<void()>& drawWindowMenu, bool& showPreferences)
    {
        static bool fullscreen = true;
        static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

        ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            windowFlags |= ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove;
            windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus |
                           ImGuiWindowFlags_NoNavFocus;
        }
        else
        {
            dockspaceFlags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
        }

        if (dockspaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
            windowFlags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace Demo", nullptr, windowFlags);
        ImGui::PopStyleVar();
        if (fullscreen)
            ImGui::PopStyleVar(2);

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem("Console", nullptr, &EditorWindows::Console());
                ImGui::MenuItem("Shadow Map", nullptr, &EditorWindows::ShadowMap());
                ImGui::MenuItem("Skybox", nullptr, &EditorWindows::Skybox());
                if (drawWindowMenu)
                    drawWindowMenu();
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Preferences..."))
                showPreferences = true;
            ImGui::EndMenuBar();
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            const ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
        }
        ImGui::End();
    }

    ImGuiLayer::ImGuiLayer(std::string name)
        : Layer(name)
    {
    }

    void ImGuiLayer::OnAttach()
    {
        LoadPreferences();
    }

    void ImGuiLayer::OnDetach()
    {
    }

    void ImGuiLayer::OnUpdate()
    {
    }

    void ImGuiLayer::OnImGuiRender()
    {
        ShowDockSpace(m_DrawWindowMenu, m_ShowPreferences);
        DrawPreferences(m_ShowPreferences);
    }

    void ImGuiLayer::SetWindowMenu(std::function<void()> drawMenu)
    {
        m_DrawWindowMenu = std::move(drawMenu);
    }
}
