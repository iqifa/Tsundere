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

static void Near(float a, float b, const char* what, float tolerance=0.002f) {
    if (!std::isfinite(a) || std::abs(a-b)>tolerance) {
        std::cerr << what << ": " << a << " expected " << b << "\n";
        throw std::runtime_error(what);
    }
}
int main() {
try {
    Engine::Log::Init();
    for(auto target : {ShaderTarget::OpenGL,ShaderTarget::Vulkan}) {
        auto post=CompileShaderFile("res/shaders/PostProcess.shader",target);
        auto preview=CompileShaderFile("res/shaders/ViewportPreview.shader",target);
        Require(post.Success && preview.Success,"Shader compile");
        Require(post.Reflection.PushConstants.at(0).Size==32,"Post parameter layout");
        Require(preview.Reflection.PushConstants.at(0).Size==16,"Preview parameter layout");
    }
    Require(glfwInit(),"GLFW");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto window=glfwCreateWindow(8,8,"Preview regression",nullptr,nullptr);Require(window,"GL context");
    glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;Require(glewInit()==GLEW_OK,"GLEW");glGetError();
    GLuint vao,fbo;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,8,8);
    GLuint post=Program("res/shaders/PostProcess.shader"),preview=Program("res/shaders/ViewportPreview.shader");
    std::vector<float> data(8*8*4);float sample[]={2,0.25f,-0.5f,0.375f};
    for(int i=0;i<64;i++) for(int c=0;c<4;c++) data[i*4+c]=sample[c];
    GLuint input=Texture(GL_RGBA32F,8,8,GL_RGBA,data.data()), display=Texture(GL_RGBA32F,8,8);
    auto draw=[&](GLuint program,GLuint src,GLuint target,std::array<float,4> settings,std::array<float,4> controls) {
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);Require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO");
        glUseProgram(program);
        GLint loc=glGetUniformLocation(program,"pc.settings");Require(loc>=0,"Settings uniform");glUniform4fv(loc,1,settings.data());
        loc=glGetUniformLocation(program,"pc.controls");if(loc>=0)glUniform4fv(loc,1,controls.data());
        for(int i=0;i<2;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,src);}
        glDrawArrays(GL_TRIANGLES,0,3);std::array<float,4> pixel;
        glReadPixels(4,4,1,1,GL_RGBA,GL_FLOAT,pixel.data());Require(glGetError()==GL_NO_ERROR,"GL error");return pixel;
    };
    auto result=draw(post,input,display,{0,0,5,0},{0,1,0,0});Near(result[0],2.f/3,"Exposure 0");
    result=draw(post,input,display,{0,0,5,0},{1,1,0,0});Near(result[0],0.8f,"Exposure +1");
    result=draw(post,input,display,{0,0,0,0},{10,0,0,0});Near(result[0],0,"Bloom threshold");
    result=draw(post,input,display,{0,0,0,0},{0,0,0,0});Near(result[0],2,"Bloom knee zero finite");
    result=draw(post,input,display,{0,0,2,0},{0.5f,0,0,0});Near(result[0],3,"Bloom strength");
    result=draw(post,input,display,{0,0,6,0},{1,1,1,0});Near(result[0],result[1],"Saturation zero");Near(result[1],result[2],"Saturation zero");
    for(int effect : {1,3,4,8,9,10,11}) {
        result=draw(post,input,display,{0,0,float(effect),0},{0,1,0,0});
        for(int c=0;c<4;c++) Near(result[c],sample[c],"Neutral post effect",0.003f);
    }
    GLenum formats[]={GL_RGBA8,GL_R8,GL_R16F,GL_RG16F,GL_RGBA16F,GL_RGBA32F};
    int channels[]={4,1,1,2,4,4};
    for(int f=0;f<6;f++) {
        GLuint storage=Texture(formats[f],8,8,channels[f]==1?GL_RED:channels[f]==2?GL_RG:GL_RGBA);
        auto stored=draw(preview,input,storage,{-1,0,1,0},{});
        for(int c=0;c<4;c++) {
            float expected=c<channels[f]?sample[c]:(c==3?1.f:0.f);
            if(f<=1) expected=std::round(std::clamp(expected,0.f,1.f)*255)/255;
            Near(stored[c],expected,"Actual storage format");
        }
        for(int channel=0;channel<6;channel++) {
            auto out=draw(preview,storage,display,{float(channel),-1,3,0},{});
            for(int c=0;c<3;c++) Near(out[c],(stored[channel>=2?channel-2:c]+1)/4,"Channel visualization");
            Near(out[3],channel==1?stored[3]:1,"Channel alpha");
        }
        glDeleteTextures(1,&storage);
    }
    std::cout<<"PASS: GL/Vulkan compilation, post effect parameters, 6 actual formats x 6 channel views\n";
    glfwDestroyWindow(window);glfwTerminate();return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
