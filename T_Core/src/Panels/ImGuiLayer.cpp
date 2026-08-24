#include "ImGuiLayer.h"

namespace Engine
{
    void ShowDockSpace()
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
    }

    void ImGuiLayer::OnDetach()
    {
    }

    void ImGuiLayer::OnUpdate()
    {
    }

    void ImGuiLayer::OnImGuiRender()
    {
        static bool show = true;
        ImGui::ShowDemoWindow(&show);
        ShowDockSpace();
    }
}
