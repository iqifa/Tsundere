#pragma once
#include "Pipeline/RenderPassRegistry.h"
#include "Pipeline/RenderGraphPass.h"
#include <vector>
#include <string>

// ========================================
// Render Pass 管理器
// 提供运行时控制和可视化功能
// ========================================
class RenderPassManager
{
public:
    RenderPassManager() = default;

    // 创建所有 Pass（根据启用状态）
    void RebuildPasses(const PassCreationContext& ctx)
    {
        m_Context = ctx;
        m_ActivePasses.clear();

        auto& registry = RenderPassRegistry::Get();
        auto& allPasses = registry.GetAllPasses();

        // 按优先级排序
        std::vector<const PassMetadata*> sorted;
        for (auto& [name, meta] : allPasses)
        {
            sorted.push_back(&meta);
        }
        std::sort(sorted.begin(), sorted.end(), [](const PassMetadata* a, const PassMetadata* b) {
            return a->priority < b->priority;
        });

        // 创建 Pass 实例
        for (auto* meta : sorted)
        {
            PassState state;
            state.metadata = meta;
            state.enabled = meta->enabledByDefault;
            state.instance = nullptr;  // 延迟创建

            m_ActivePasses.push_back(state);
        }
    }

    // 获取当前启用的 Pass 实例列表（用于添加到 RenderGraph）
    std::vector<Ref<RenderGraphPass>> GetEnabledPasses()
    {
        std::vector<Ref<RenderGraphPass>> passes;

        for (auto& state : m_ActivePasses)
        {
            if (!state.enabled)
                continue;

            // 延迟创建实例（只在启用时创建）
            if (!state.instance)
            {
                state.instance = state.metadata->factory(m_Context);
            }

            if (state.instance)
                passes.push_back(state.instance);
        }

        return passes;
    }

    // ImGui 控制面板
    void OnImGuiRender()
    {
        ImGui::Begin("Render Pass Manager");

        ImGui::Text("Total Passes: %zu", m_ActivePasses.size());
        ImGui::Separator();

        // 分类显示
        std::unordered_map<std::string, std::vector<PassState*>> categorized;
        for (auto& state : m_ActivePasses)
        {
            categorized[state.metadata->category].push_back(&state);
        }

        for (auto& [category, states] : categorized)
        {
            if (ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();

                for (auto* state : states)
                {
                    ImGui::PushID(state->metadata->name.c_str());

                    // 启用/禁用复选框
                    bool prevEnabled = state->enabled;
                    ImGui::Checkbox("##enabled", &state->enabled);
                    ImGui::SameLine();

                    // Pass 名称和信息
                    ImGui::Text("%s", state->metadata->name.c_str());

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Priority: %d", state->metadata->priority);
                        ImGui::Text("Default: %s", state->metadata->enabledByDefault ? "Enabled" : "Disabled");
                        ImGui::EndTooltip();
                    }

                    // 状态指示器
                    ImGui::SameLine();
                    if (state->instance && state->enabled)
                    {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[Active]");
                    }
                    else if (state->enabled)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[Pending]");
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[Disabled]");
                    }

                    // 如果启用状态改变，标记需要重建
                    if (prevEnabled != state->enabled)
                    {
                        m_NeedsRebuild = true;

                        // 如果禁用，释放实例
                        if (!state->enabled)
                        {
                            state->instance.reset();
                        }
                    }

                    ImGui::PopID();
                }

                ImGui::Unindent();
            }
        }

        ImGui::Separator();

        // 批量操作
        if (ImGui::Button("Enable All"))
        {
            for (auto& state : m_ActivePasses)
                state.enabled = true;
            m_NeedsRebuild = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable All"))
        {
            for (auto& state : m_ActivePasses)
            {
                state.enabled = false;
                state.instance.reset();
            }
            m_NeedsRebuild = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to Default"))
        {
            for (auto& state : m_ActivePasses)
            {
                state.enabled = state.metadata->enabledByDefault;
                if (!state.enabled)
                    state.instance.reset();
            }
            m_NeedsRebuild = true;
        }

        ImGui::End();
    }

    // 检查是否需要重建 RenderGraph
    bool NeedsRebuild() const { return m_NeedsRebuild; }
    void ClearRebuildFlag() { m_NeedsRebuild = false; }

    // 按名称启用/禁用 Pass
    void SetPassEnabled(const std::string& passName, bool enabled)
    {
        for (auto& state : m_ActivePasses)
        {
            if (state.metadata->name == passName)
            {
                if (state.enabled != enabled)
                {
                    state.enabled = enabled;
                    m_NeedsRebuild = true;
                    if (!enabled)
                        state.instance.reset();
                }
                break;
            }
        }
    }

    // 检查 Pass 是否启用
    bool IsPassEnabled(const std::string& passName) const
    {
        for (auto& state : m_ActivePasses)
        {
            if (state.metadata->name == passName)
                return state.enabled;
        }
        return false;
    }

private:
    struct PassState
    {
        const PassMetadata* metadata = nullptr;
        bool enabled = false;
        Ref<RenderGraphPass> instance;
    };

    std::vector<PassState> m_ActivePasses;
    PassCreationContext m_Context;
    bool m_NeedsRebuild = false;
};
