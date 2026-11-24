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

Ref<RHIShader> RHIShader::Create(const std::string&) { return {}; }
Ref<RHIShader> RHIShader::Create(const std::string&, const std::string&) { return {}; }
std::shared_mutex ShaderLibiray::s_Mutex;
std::unordered_map<std::string, Ref<RHIShader>> ShaderLibiray::m_Shaders;


static void Require(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
static GLuint Program(const char* path) {
    auto c = CompileShaderFile(path, ShaderTarget::OpenGL);
    Require(c.Success, path);
    GLuint program = glCreateProgram();
    for (auto& [stage, source] : c.Glsl) {
        GLuint shader = glCreateShader(stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
        const char* text = source.c_str(); glShaderSource(shader, 1, &text, nullptr); glCompileShader(shader);
        GLint ok; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[8192]; glGetShaderInfoLog(shader, sizeof(log), nullptr, log); throw std::runtime_error(log); }
        glAttachShader(program, shader); glDeleteShader(shader);
    }
    glLinkProgram(program); GLint ok; glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) { char log[8192]; glGetProgramInfoLog(program, sizeof(log), nullptr, log); throw std::runtime_error(log); }
    return program;
}
static GLuint Texture(GLenum format, int w, int h, GLenum channels=GL_RGBA, const float* data=nullptr) {
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,channels,GL_FLOAT,data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    return t;
}
int main() {
try {
    Engine::Log::Init();
    for (auto target : {ShaderTarget::OpenGL, ShaderTarget::Vulkan})
        for (auto path : {"res/shaders/TAA.shader", "res/shaders/Transparent.shader"})
            Require(CompileShaderFile(path, target).Success, path);
    Require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(8,8,"TAA depth tests",nullptr,nullptr);
    Require(window, "GL context"); glfwMakeContextCurrent(window);
    glewExperimental=GL_TRUE; Require(glewInit()==GLEW_OK,"GLEW"); glGetError();
    GLuint program=Program("res/shaders/TAA.shader"); glUseProgram(program);
    GLuint vao,vbo,ubo,fbo; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    float vertices[]={-1,-1,0,0, 3,-1,2,0, -1,3,0,2};
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    for(int i=0;i<2;++i) { glEnableVertexAttribArray(i); glVertexAttribPointer(i,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(i*2*sizeof(float))); }
    std::array<float,8*8*4> colors{};
    std::array<float,8*8> depths{};
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
        int p=y*8+x; colors[p*4]=float(x)/8; colors[p*4+3]=1;
        depths[p]=(x<4?0.25f:1.0f);
    }
    GLuint source=Texture(GL_RGBA32F,8,8,GL_RGBA,colors.data());
    std::array<float,8*8*4> zeros{};
    GLuint velocity=Texture(GL_RGBA32F,8,8,GL_RGBA,zeros.data());
    GLuint depth=Texture(GL_DEPTH_COMPONENT32F,8,8,GL_DEPTH_COMPONENT,depths.data());
    GLuint output=Texture(GL_RGBA32F,8,8);
    GLuint outputDepth=Texture(GL_DEPTH_COMPONENT32F,8,8,GL_DEPTH_COMPONENT);
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,outputDepth,0);
    Require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"TAA framebuffer");
    GLuint inputs[]={source,source,velocity,depth};
    for(int i=0;i<4;++i) {glActiveTexture(GL_TEXTURE10+i); glBindTexture(GL_TEXTURE_2D,inputs[i]);}
    glGenBuffers(1,&ubo); glBindBuffer(GL_UNIFORM_BUFFER,ubo); glBindBufferBase(GL_UNIFORM_BUFFER,0,ubo);
    glViewport(0,0,8,8); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
    // Signed offsets cross a silhouette; zero verifies the unjittered grid.
    for(float jitter : {-0.75f,0.0f,0.75f}) {
        std::array<float,36> params{};
        for(int i=0;i<4;++i) {params[i*5]=1; params[16+i*5]=1;}
        params[32]=jitter/8;
        glBufferData(GL_UNIFORM_BUFFER,sizeof(params),params.data(),GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES,0,3);
        std::array<float,8*8> actualDepth{};
        std::array<float,8*8*4> actualColor{};
        glReadPixels(0,0,8,8,GL_DEPTH_COMPONENT,GL_FLOAT,actualDepth.data());
        glReadPixels(0,0,8,8,GL_RGBA,GL_FLOAT,actualColor.data());
        for(int x=1;x<7;++x) {
            int sx=int(std::floor(float(x)+0.5f+jitter));
            Require(std::abs(actualDepth[3*8+x]-depths[3*8+sx])<1e-5f,"Stable depth sampled at wrong jitter offset");
            Require(std::abs(actualColor[(3*8+x)*4]-float(sx)/8)<1e-5f,"Color/depth grids disagree");
        }
        Require(glGetError()==GL_NO_ERROR,"TAA draw/readback GL error");
    }
    glfwDestroyWindow(window); glfwTerminate();
    std::cout << "PASS: Vulkan/OpenGL shaders; stable TAA color/depth, signed/zero jitter, silhouette and clear background\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}