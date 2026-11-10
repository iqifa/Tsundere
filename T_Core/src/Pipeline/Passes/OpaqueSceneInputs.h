#pragma once

#include "Pipeline/RenderGraphPass.h"

struct OpaqueSceneInputs
{
	const char* colorName;
	const char* depthName;
	RGTextureHandle color;
	RGTextureHandle depth;
	RGTextureHandle velocity;

	bool IsValid() const
	{
		return color.id != InvalidResourceId && depth.id != InvalidResourceId &&
			velocity.id != InvalidResourceId;
	}
};

inline OpaqueSceneInputs ReadOpaqueSceneInputs(RenderGraphBuilder& builder)
{
	const auto read = [&](const char* color, const char* depth, const char* velocity)
	{
		return OpaqueSceneInputs{ color, depth, builder.ReadTexture(color),
			builder.ReadTexture(depth), builder.ReadTexture(velocity) };
	};
	const auto deferred = read("DeferredLight.Light", "Gbuffer.Depth", "Gbuffer.Velocity");
	if (deferred.IsValid())
		return deferred;
	return read("Geometry.SceneColor", "Geometry.Depth", "Geometry.Velocity");
}
