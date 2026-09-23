#pragma once
#include "Pipeline/RenderGraphPass.h"
#include <functional>
#include <unordered_map>
#include <vector>

// Pass 创建参数（统一接口）
struct PassCreationContext
{
    Ref<Scene> scene;
    Ref<RGFrameData> frameData;
    uint32_t width;
    uint32_t height;
};

// Pass 工厂函数签名
using PassFactory = std::function<Ref<RenderGraphPass>(const PassCreationContext&)>;

// Pass 元数据
struct PassMetadata
{
    std::string name;           // Pass 类型名（如 "Geometry", "TAA"）
    std::string category;       // 分类（如 "Lighting", "PostProcess"）
    int priority;              // 优先级（数字越小越先执行的倾向）
    bool enabledByDefault;     // 默认是否启用
    PassFactory factory;       // 创建函数
};

// ========================================
// Pass 注册表（单例）
// ========================================
class RenderPassRegistry
{
public:
    static RenderPassRegistry& Get()
    {
        static RenderPassRegistry instance;
        return instance;
    }

    // 注册 Pass（在静态初始化时调用）
    void RegisterPass(const PassMetadata& metadata)
    {
        m_RegisteredPasses[metadata.name] = metadata;
    }

    // 创建单个 Pass
    Ref<RenderGraphPass> CreatePass(const std::string& name, const PassCreationContext& ctx)
    {
        auto it = m_RegisteredPasses.find(name);
        if (it == m_RegisteredPasses.end())
            return nullptr;
        return it->second.factory(ctx);
    }

    // 创建所有默认启用的 Pass
    std::vector<Ref<RenderGraphPass>> CreateDefaultPasses(const PassCreationContext& ctx)
    {
        std::vector<Ref<RenderGraphPass>> passes;

        // 按优先级排序（注意：这只是创建顺序，实际执行顺序由 RDG 决定）
        std::vector<PassMetadata*> sorted;
        for (auto& [name, meta] : m_RegisteredPasses)
        {
            if (meta.enabledByDefault)
                sorted.push_back(&meta);
        }

        std::sort(sorted.begin(), sorted.end(), [](const PassMetadata* a, const PassMetadata* b) {
            return a->priority < b->priority;
        });

        for (auto* meta : sorted)
        {
            if (auto pass = meta->factory(ctx))
                passes.push_back(pass);
        }

        return passes;
    }

    // 按分类创建 Pass
    std::vector<Ref<RenderGraphPass>> CreatePassesByCategory(
        const std::string& category,
        const PassCreationContext& ctx)
    {
        std::vector<Ref<RenderGraphPass>> passes;
        for (auto& [name, meta] : m_RegisteredPasses)
        {
            if (meta.category == category)
            {
                if (auto pass = meta.factory(ctx))
                    passes.push_back(pass);
            }
        }
        return passes;
    }

    // 列出所有已注册的 Pass 名称
    std::vector<std::string> GetRegisteredPassNames() const
    {
        std::vector<std::string> names;
        for (auto& [name, _] : m_RegisteredPasses)
            names.push_back(name);
        return names;
    }

    const std::unordered_map<std::string, PassMetadata>& GetAllPasses() const
    {
        return m_RegisteredPasses;
    }

private:
    RenderPassRegistry() = default;
    std::unordered_map<std::string, PassMetadata> m_RegisteredPasses;
};

// ========================================
// 自动注册宏（在 Pass 定义文件中使用）
// ========================================
#define REGISTER_RENDER_PASS(PassClass, PassName, Category, Priority, EnabledByDefault) \
    namespace { \
        struct PassClass##_AutoRegister { \
            PassClass##_AutoRegister() { \
                PassMetadata meta; \
                meta.name = PassName; \
                meta.category = Category; \
                meta.priority = Priority; \
                meta.enabledByDefault = EnabledByDefault; \
                meta.factory = [](const PassCreationContext& ctx) -> Ref<RenderGraphPass> { \
                    auto pass = CreateRef<PassClass>(ctx.scene, ctx.frameData); \
                    if (auto resizeable = std::dynamic_pointer_cast<PassClass>(pass)) { \
                        resizeable->SetViewportSize(ctx.width, ctx.height); \
                    } \
                    return pass; \
                }; \
                RenderPassRegistry::Get().RegisterPass(meta); \
            } \
        }; \
        static PassClass##_AutoRegister g_##PassClass##_AutoRegister; \
    }

// 简化版宏（使用默认参数）
#define REGISTER_PASS_SIMPLE(PassClass, Category) \
    REGISTER_RENDER_PASS(PassClass, #PassClass, Category, 100, true)
