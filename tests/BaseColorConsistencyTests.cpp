#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "Platform/ShaderCompiler.h"
#include "Debug/Debug.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace Engine {
Ref<spdlog::logger> Log::s_CoreLogger;
Ref<spdlog::logger> Log::s_ClientLogger;
void Log::Init() { s_CoreLogger = spdlog::stderr_color_mt("base-color-test"); }
void Log::CopyClientConsole(std::vector<ConsoleLine>&) {}
void Log::ClearClientConsole() {}
unsigned char* LoadImageSTB(const char*, int*, int*, int*, int, bool) { return nullptr; }
}
Ref<RHIShader> RHIShader::Create(const std::string&) { return {}; }
Ref<RHIShader> RHIShader::Create(const std::string&, const std::string&) { return {}; }
std::shared_mutex ShaderLibiray::s_Mutex;
std::unordered_map<std::string, Ref<RHIShader>> ShaderLibiray::m_Shaders;
static void Require(bool b, const char* text) { if (!b) throw std::runtime_error(text); }
static GLuint Program(const char* path) {
    auto compiled = CompileShaderFile(path, ShaderTarget::OpenGL);
    Require(compiled.Success, path);
    GLuint p = glCreateProgram();
    for (auto& [stage, source] : compiled.Glsl) {
        GLuint s = glCreateShader(stage == ShaderStage::Compute ? GL_COMPUTE_SHADER :
            stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
        const char* text = source.c_str(); glShaderSource(s,1,&text,nullptr); glCompileShader(s);
        GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
        if (!ok) { char log[8192]; glGetShaderInfoLog(s,sizeof(log),nullptr,log); throw std::runtime_error(log); }
        glAttachShader(p,s); glDeleteShader(s);
    }
    glLinkProgram(p); GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if (!ok) { char log[8192]; glGetProgramInfoLog(p,sizeof(log),nullptr,log); throw std::runtime_error(log); }
    return p;
}
static GLuint Texture(GLenum format, int width=1, int height=1, const void* data=nullptr) {
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,format,width,height,0,GL_RGBA,data ? GL_UNSIGNED_BYTE : GL_FLOAT,data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    return t;
}
int main() { try {
    Engine::Log::Init();
    for (auto target : {ShaderTarget::OpenGL, ShaderTarget::Vulkan}) {
        for (auto path : {"res/shaders/MaterialTest.shader", "res/shaders/GBuffer.shader",
             "res/shaders/PathTraceBaseColor.shader", "tests/shaders/BaseColorPT.shader",
             "res/shaders/PathTraceBVH.shader", "res/shaders/PathTraceBVHSky.shader"})
            Require(CompileShaderFile(path,target).Success,path);
        if (target == ShaderTarget::Vulkan)
            for (auto path : {"res/shaders/PathTraceRayQuery.shader", "res/shaders/PathTraceRayQuerySky.shader"})
                Require(CompileShaderFile(path,target).Success,path);
        auto pt = CompileShaderFile("res/shaders/PathTraceBVH.shader",target);
        auto block = pt.Reflection.FindStorageBlock(2);
        Require(block && block->Members[0].ArrayStride == 80,"PT material ABI mismatch");
        Require(pt.Reflection.FindStorageBlock(5),"PT texture cache missing");
    }
    Require(glfwInit(),"GLFW"); glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);
    auto window=glfwCreateWindow(4,4,"color test",nullptr,nullptr); Require(window,"GL context");
    glfwMakeContextCurrent(window); glewExperimental=GL_TRUE; Require(glewInit()==GLEW_OK,"GLEW"); glGetError();
    auto forward=Program("res/shaders/MaterialTest.shader"), deferred=Program("res/shaders/GBuffer.shader");
    auto bake=Program("res/shaders/PathTraceBaseColor.shader"), pt=Program("tests/shaders/BaseColorPT.shader");
    GLuint vao,vbo; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    float vertices[]={-1,-1,0, 3,-1,0, -1,3,0};
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,nullptr);
    glVertexAttrib3f(1,0,0,1);
    float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for (auto p : {forward,deferred}) {
        glUseProgram(p); glUniformMatrix4fv(glGetUniformLocation(p,"pc.model"),1,GL_FALSE,identity);
        glUniformMatrix4fv(glGetUniformLocation(p,"pc.prevModel"),1,GL_FALSE,identity);
    }
    GLuint buffers[5]; glGenBuffers(5,buffers);
    auto upload=[&](int i,int binding,const void* data,size_t size) {
        glBindBuffer(GL_UNIFORM_BUFFER,buffers[i]); glBufferData(GL_UNIFORM_BUFFER,size,data,GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER,binding,buffers[i]);
    };
    float camera[48]; for(int i=0;i<48;i++) camera[i]=identity[i%16]; upload(0,2,camera,sizeof(camera));
    std::array<float,44> frame{}; frame[2]=1; frame[4]=frame[5]=frame[6]=frame[8]=1; frame[14]=1;
    std::array<float,32> material{}; material[0]=0.5f; material[1]=material[2]=0.75f; material[3]=1;
    material[8]=material[9]=1; material[12]=1; // roughness = 1 disables MaterialTest specular
    GLuint fbo; glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    GLuint attachments[5]; GLenum draws[5];
    for(int i=0;i<5;i++) { attachments[i]=Texture(GL_RGBA32F); draws[i]=GL_COLOR_ATTACHMENT0+i;
        glFramebufferTexture2D(GL_FRAMEBUFFER,draws[i],GL_TEXTURE_2D,attachments[i],0); }
    glDrawBuffers(5,draws); Require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO");
    glViewport(0,0,1,1);
    const unsigned char pixels[]={128,64,32,255, 255,0,64,128, 32,192,128,64, 0,128,255,0};
    GLuint raw=Texture(GL_RGBA8,2,2,pixels), srgb=Texture(GL_SRGB8_ALPHA8,2,2,pixels);
    GLuint cache; glGenBuffers(1,&cache); glBindBuffer(GL_SHADER_STORAGE_BUFFER,cache);
    glBufferData(GL_SHADER_STORAGE_BUFFER,16*4*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
    GLuint lights; glGenBuffers(1,&lights); glBindBuffer(GL_SHADER_STORAGE_BUFFER,lights);
    const float noLights[16]={}; glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(noLights),noLights,GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,lights);
    auto render=[&](GLuint program,int output,float u,float v,uint32_t flags) {
        glVertexAttrib2f(2,u,v); std::memcpy(&material[7],&flags,4); upload(1,3,material.data(),sizeof(material));
        if(program==forward) upload(2,1,frame.data(),sizeof(frame));
        else { int zero[4]={}; upload(2,1,zero,sizeof(zero)); }
        glUseProgram(program); glDrawArrays(GL_TRIANGLES,0,3);
        std::array<float,4> result; glReadBuffer(GL_COLOR_ATTACHMENT0+output);
        glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,result.data()); return result;
    };
    for (auto wrap : {GL_REPEAT, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER})
    for (auto filter : {GL_LINEAR, GL_NEAREST})
    for (int hardware=0;hardware<2;hardware++) {
        GLuint source=hardware?srgb:raw; uint32_t flags=(hardware?0u:1u)|(filter==GL_NEAREST?2u:0u);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,source);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,wrap); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,wrap);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,cache); glUseProgram(bake);
        glUniform4ui(glGetUniformLocation(bake,"pc.info"),0,4,4,flags); glDispatchCompute(1,1,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glActiveTexture(GL_TEXTURE10); glBindTexture(GL_TEXTURE_2D,source);
        for (auto uv : {std::array<float,2>{0.25f,0.25f}, {0.25f,0.75f}, {0.5f,0.5f}, {-0.1f,1.1f}, {-3,4}}) {
            // Nontrivial transform tests UV orientation, repeat, negative coordinates and offset.
            material[8]=1.5f; material[9]=0.75f; material[10]=-0.125f; material[11]=0.0625f;
            auto a=render(forward,0,uv[0],uv[1],flags), b=render(deferred,1,uv[0],uv[1],flags);
            std::array<float,12> test={uv[0],uv[1],material[8],material[9],material[10],material[11],material[0],material[1]};
            uint32_t info[]={0,2,2,flags|(wrap==GL_REPEAT?12u:0u)}; std::memcpy(test.data()+8,info,sizeof(info));
            upload(3,0,test.data(),sizeof(test)); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,cache);
            glBindImageTexture(1,attachments[4],0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA32F);
            glUseProgram(pt); glDispatchCompute(1,1,1); glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);
            std::array<float,4> c; glReadBuffer(GL_COLOR_ATTACHMENT4); glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,c.data());
            for(int ch=0;ch<3;ch++) {
                Require(std::abs(a[ch]-b[ch])<0.0001f,"Forward/GBuffer base color differs");
                Require(std::abs(a[ch]-c[ch])<0.002f,"Raster/PT base color differs");
            }
        }
        material[8]=material[9]=1; material[10]=material[11]=0;
        auto gray=render(forward,0,0.25f,0.25f,flags);
        const float expected=0.5f*std::pow((128.f/255+0.055f)/1.055f,2.4f);
        Require(std::abs(gray[0]-expected)<0.001f,"sRGB gray not decoded exactly once / linear tint changed");
    }
    Require(glGetError()==GL_NO_ERROR,"GL error");
    std::cout<<"PASS: GL/Vulkan shaders and ABI; forward/GBuffer/PT color, sRGB/UNORM, tint, UV, wrap, filtering\n";
    glfwDestroyWindow(window); glfwTerminate(); return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; } }
