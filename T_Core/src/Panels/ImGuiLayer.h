#pragma once
#ifndef GUILAYER
#define GUILAYER

#include "ExternalFiles.h"
#include "Core/Layer/LayerStack.h"
#include <functional>

namespace Engine
{
    class T_API ImGuiLayer : public Layer
    {
    public:
        explicit ImGuiLayer(std::string name = "Layer");
        ~ImGuiLayer() = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate() override;
        void OnImGuiRender() override;
        void SetWindowMenu(std::function<void()> drawMenu);

        void BlockEvent(bool block) { (void)block; }

    private:
        std::function<void()> m_DrawWindowMenu;
    };
}

#endif
