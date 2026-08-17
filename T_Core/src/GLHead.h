// GLHead.h — umbrella include for all graphics platform headers
// Includes RHI interfaces + backend implementations + legacy GL wrappers

#include "Platform/RenderAPI.h"           // Backend selection + RHI includes
#include "Platform/GL/Renderer.h"         // GLCall/GLClearError macros + Renderer class
#include "Platform/GL/IndexBuffer.h"
#include "Platform/GL/VertexBuffer.h"
#include "Platform/GL/GLShader.h"
#include "Platform/GL/VertexArray.h"
#include "Platform/GL/VertexBufferLayout.h"
#include "Platform/GL/Texture.h"