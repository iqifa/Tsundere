// Single translation unit providing the stb_image implementation.
// Previously lived in CubeMap.cpp; moved here so every stbi_* consumer
// (GLTexture2D, GLTextureCube, Texture, ResourceLoader, RenderGraph)
// links against one definition.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb_image.h"
