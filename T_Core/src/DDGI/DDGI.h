#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <vector>

// ============================================================================
// DDGI (Dynamic Diffuse Global Illumination) — core data structures
// ============================================================================

namespace DDGI
{
    // GPU-compatible probe ray data (std430 layout, uploaded to SSBO slot 0)
    // One per probe in the volume.
    struct alignas(16) GPUProbeRayData
    {
        glm::vec4 Position_Radius;  // xyz = world-space probe position, w = probe influence radius
    };

    // Atlas layout helper: given a probe count, computes probes-per-row so the
    // 2D atlas stays roughly square (minimizes memory).
    inline int ComputeProbesPerRow(int totalProbes)
    {
        int perRow = (int)std::ceil(std::sqrt((float)totalProbes));
        if (perRow < 1) perRow = 1;
        return perRow;
    }

    // Convert a linear probe index to world-space position given the grid config.
    // The grid is laid out as: x varies fastest, then y, then z.
    inline glm::vec3 ProbeWorldPos(glm::ivec3 gridSize,
                                   glm::vec3  gridOrigin,
                                   float      spacing,
                                   glm::ivec3 scrollOffset,
                                   int        probeIndex)
    {
        int z = probeIndex / (gridSize.x * gridSize.y);
        int r = probeIndex % (gridSize.x * gridSize.y);
        int y = r / gridSize.x;
        int x = r % gridSize.x;

        // Probe centers are at half-cell offsets (matches shader)
        glm::vec3 offset = glm::vec3(float(x + scrollOffset.x) + 0.5f,
                                     float(y + scrollOffset.y) + 0.5f,
                                     float(z + scrollOffset.z) + 0.5f) * spacing;
        return gridOrigin + offset;
    }

    // Fill a vector of GPUProbeRayData for all probes in the volume.
    inline void BuildProbeRayData(std::vector<GPUProbeRayData>& dst,
                                  glm::ivec3 gridSize,
                                  glm::vec3  gridOrigin,
                                  float      spacing,
                                  float      probeRadius,
                                  glm::ivec3 scrollOffset)
    {
        int total = gridSize.x * gridSize.y * gridSize.z;
        dst.resize(total);
        for (int i = 0; i < total; ++i)
        {
            glm::vec3 pos = ProbeWorldPos(gridSize, gridOrigin, spacing, scrollOffset, i);
            dst[i].Position_Radius = glm::vec4(pos, probeRadius);
        }
    }
}
