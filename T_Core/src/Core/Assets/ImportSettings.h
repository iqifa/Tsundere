#pragma once

#include "Platform/RHI/RHITexture.h"
#include <cstdint>

namespace Engine {

// Import settings stored as "import.*" lines in the asset's .meta file.
// Defaults are fixed constants (never inferred from file names or usage) and
// match how files loaded before import settings existed. Sync writes them into
// the .meta of any asset that has none, with confirmed = false, so the editor
// can point out assets nobody has reviewed yet.
// A file read by the loader that has no .meta (e.g. outside Assets/) uses the
// defaults without writing anything.

struct TextureImportSettings
{
	// true: upload as RGBA8_SRGB (hardware decode). false: RGBA8_UNORM; color
	// textures are then decoded by the material shaders where needed.
	bool srgb = false;
	bool flipY = true;
	FilterMode filter = FilterMode::Linear;
	// Model UVs often tile beyond [0,1]; clamping stretches edge texels.
	WrapMode wrapS = WrapMode::Repeat;
	WrapMode wrapT = WrapMode::Repeat;
	bool confirmed = false;

	// Settings that change the imported texture (confirmed is ignored).
	bool SameImport(const TextureImportSettings& o) const
	{
		return srgb == o.srgb && flipY == o.flipY && filter == o.filter &&
			wrapS == o.wrapS && wrapT == o.wrapT;
	}

	void ApplyTo(Texture2DDesc& desc) const
	{
		desc.format = srgb ? Format::RGBA8_SRGB : Format::RGBA8_UNORM;
		desc.minFilter = filter;
		desc.magFilter = filter;
		desc.wrapS = wrapS;
		desc.wrapT = wrapT;
	}
};

enum class ModelNormalMode : uint8_t
{
	SmoothIfMissing,  // keep authored normals; generate smooth ones where absent
	FlatIfMissing,    // keep authored normals; generate flat ones where absent
	RecomputeSmooth,  // discard authored normals and generate smooth ones
	RecomputeFlat     // discard authored normals and generate flat ones
};

struct ModelImportSettings
{
	bool flipUVs = true;
	bool convertToLeftHanded = true;
	ModelNormalMode normals = ModelNormalMode::SmoothIfMissing;
	// Max angle between faces that still share a smoothed normal (degrees).
	float smoothingAngle = 175.0f;
	bool calcTangents = true;
	// Welds identical vertices inside each mesh. Mesh count and order are
	// unchanged, so per-submesh state in the .meta stays valid.
	bool joinVertices = false;
	bool confirmed = false;

	bool SameImport(const ModelImportSettings& o) const
	{
		return flipUVs == o.flipUVs && convertToLeftHanded == o.convertToLeftHanded &&
			normals == o.normals && smoothingAngle == o.smoothingAngle &&
			calcTangents == o.calcTangents && joinVertices == o.joinVertices;
	}
};

} // namespace Engine
