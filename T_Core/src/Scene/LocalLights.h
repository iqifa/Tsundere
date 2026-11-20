#pragma once
#include "Scene/Scene.h"
#include <vector>

// Matches Light in shaders/include/Lighting.glsl (std430, four vec4s).
struct LocalLightData
{
    glm::vec4 directionAmbient;
    glm::vec4 colorIntensity;
    glm::vec4 positionType;
    glm::vec4 params;
};
static_assert(sizeof(LocalLightData) == 64);

inline std::vector<LocalLightData> GatherLocalLights(const Scene& scene)
{
    std::vector<LocalLightData> result;
    for (auto [id, transform, light] : scene.m_Registry.view<const Component::Transform, const Component::Light>().each())
    {
        using Type = Component::Light::LightType;
        if (!scene.IsActive(id) || (light.type != Type::Point && light.type != Type::Spot && light.type != Type::Area)) continue;
        result.push_back({ {Component::LightDirection(transform), 0.0f}, {light.Color, light.Intensity},
            {transform.Position, static_cast<float>(light.type)},
            {light.Params.Data[0], light.Params.Data[1], light.Params.Data[2], light.Params.Data[3]} });
        // Shape is stored as uint32_t in the component union, not float bits.
        if (light.type == Type::Area)
            result.back().params = {light.Params.Area.Width, light.Params.Area.Height,
                static_cast<float>(light.Params.Area.Shape), 0.0f};
    }
    return result;
}
