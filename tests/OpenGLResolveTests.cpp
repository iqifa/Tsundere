#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cmath>
#include <array>
#include <filesystem>
#include <stdexcept>
#include "Platform/ShaderCompiler.h"
#include "Debug/Debug.h"
#include <fstream>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Engine {
Ref<spdlog::logger> Log::s_CoreLogger;
Ref<spdlog::logger> Log::s_ClientLogger;
void Log::Init() { s_CoreLogger = spdlog::stderr_color_mt("shader-test"); }
void Log::CopyClientConsole(std::vector<ConsoleLine>&) {}
void Log::ClearClientConsole() {}
unsigned char* LoadImageSTB(const char*, int*, int*, int*, int, bool) { return nullptr; }
}


#include "Platform/GL/GLTexture2D.h"
#include "Platform/GL/GLFramebuffer.h"
#include "Platform/GL/GLCommandBuffer.h"
#include <cassert>
Ref<RHIShader> RHIShader::Create(const std::string&) { return {}; }
Ref<RHIShader> RHIShader::Create(const std::string&, const std::string&) { return {}; }
std::shared_mutex ShaderLibiray::s_Mutex;
std::unordered_map<std::string, Ref<RHIShader>> ShaderLibiray::m_Shaders;
extern "C" void stbi_image_free(void*) {}
void GLClearError() { while (glGetError() != GL_NO_ERROR) {} }
bool GLLogCall(const char* call, const char*, int) {
    const auto error = glGetError();
    if (error) throw std::runtime_error(std::string(call) + " GL error " + std::to_string(error));
    return true;
}
static void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static Ref<RHITexture2D> Texture(Format format, unsigned samples, unsigned size) {
    Texture2DDesc d; d.width=d.height=size; d.format=format; d.sampleCount=samples;
    return CreateRef<GLTexture2D>(d);
}
static void Check(unsigned samples, unsigned size) {
    auto color=Texture(Format::RGBA8_UNORM,samples,size);
    auto velocity=Texture(Format::RG16F,samples,size);
    auto depth=Texture(Format::D24_UNORM_S8_UINT,samples,size);
    Require(color->GetSampleCount()==samples,"Sample count discarded");
    FramebufferViewDesc desc; desc.width=desc.height=size;
    desc.colorAttachments={color.get(),velocity.get()}; desc.depthAttachment=depth.get();
    desc.depthFormat=depth->GetFormat();
    auto fb=CreateRef<GLFramebufferView>(desc);
    fb->Bind(); Require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"MSAA FBO incomplete");
    auto outColor=Texture(Format::RGBA8_UNORM,1,size);
    auto outVelocity=Texture(Format::RG16F,1,size);
    auto outDepth=Texture(Format::D24_UNORM_S8_UINT,1,size);
    GLCommandBuffer cmd;
    for(int frame=0; frame<2; ++frame) {
        RenderPassBeginInfo info; info.colorAttachments.resize(2);
        for(auto& c:info.colorAttachments)c.loadOp=AttachmentLoadOp::Clear;
        info.colorAttachments[0].clearValue={0.25f,0.5f,0.75f,1};
        info.colorAttachments[1].clearValue={-0.125f,0.25f,0,0};
        info.hasDepth=true; info.depth.loadOp=AttachmentLoadOp::Clear; info.depth.clearDepth=0.375f;
        if(samples>1) {
            info.colorAttachments[0].resolveTarget=outColor.get();
            info.colorAttachments[1].resolveTarget=outVelocity.get();
            info.depth.resolveTarget=outDepth.get();
        }
        glDisable(GL_SCISSOR_TEST);
        cmd.BeginRenderPass(fb,info);
        // A UI scissor must not clip the resolve. Source MRT state must survive a second frame.
        glEnable(GL_SCISSOR_TEST); glScissor(0,0,1,1);
        cmd.EndRenderPass();
        Require(glIsEnabled(GL_SCISSOR_TEST),"Scissor state lost");
        std::vector<float> pixels(size*size*4);
        (samples>1?outColor:color)->Bind(0);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());
        Require(std::abs(pixels[(size*size-1)*4]-0.25f)<0.01f,"Scene color resolve is black/wrong");
        (samples>1?outVelocity:velocity)->Bind(0);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RG,GL_FLOAT,pixels.data());
        Require(std::abs(pixels[(size*size-1)*2]+0.125f)<0.001f,"Velocity resolve wrong");
        (samples>1?outDepth:depth)->Bind(0);
        glGetTexImage(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,GL_FLOAT,pixels.data());
        Require(std::abs(pixels[size*size-1]-0.375f)<0.001f,"Depth resolve wrong");
        Require(glGetError()==GL_NO_ERROR,"GL readback error");
    }
    color->Resize(size+4,size+4);
    Require(color->GetSampleCount()==samples && color->GetWidth()==size+4,"MSAA resize lost metadata");
}
int main() {
 try {
    Engine::Log::Init(); Require(glfwInit(),"GLFW init");
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(64,64,"Resolve tests",nullptr,nullptr); Require(window,"GL context");
    glfwMakeContextCurrent(window); glewExperimental=GL_TRUE; Require(glewInit()==GLEW_OK,"GLEW"); GLClearError();
    for(auto size:{16u,32u}) for(auto samples:{1u,4u}) Check(samples,size);
    glfwDestroyWindow(window); glfwTerminate();
    std::cout << "PASS: OpenGL 1x/4x scene color, velocity, depth, repeated frames, scissor, resize\n";
    return 0;
 } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
