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
    const char* paths[]={"res/shaders/SkyBox.shader", "res/shaders/SkyBoxColor.shader", "tests/shaders/SkyboxPT.shader"};
    for(auto target : {ShaderTarget::OpenGL,ShaderTarget::Vulkan}) {
        for(auto path : paths) Require(CompileShaderFile(path,target).Success,path);
        Require(CompileShaderFile("res/shaders/SkyboxComposite.shader",target).Success,"Sky composite compile");
        Require(CompileShaderFile("res/shaders/TAA.shader",target).Success,"TAA coverage compile");
        Require(CompileShaderFile("res/shaders/PathTraceBVHSky.shader",target).Success,"PT BVH sky compile");
    }
    Require(CompileShaderFile("res/shaders/PathTraceRayQuerySky.shader",ShaderTarget::Vulkan).Success,"PT ray query sky compile");
    Require(glfwInit(),"GLFW"); glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);
    auto window=glfwCreateWindow(4,4,"sky test",nullptr,nullptr); Require(window,"GL context");
    glfwMakeContextCurrent(window); glewExperimental=GL_TRUE; Require(glewInit()==GLEW_OK,"GLEW"); glGetError();
    GLuint programs[]={Program(paths[0]),Program(paths[1]),Program(paths[2])};
    GLuint vao,vbo; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    float vertices[]={-1,-1,1, 3,-1,1, -1,3,1};
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,nullptr);
    float camera[52]={}; for(int m=0;m<3;m++) for(int i=0;i<4;i++) camera[m*16+i*5]=1;
    GLuint ubo; glGenBuffers(1,&ubo); glBindBuffer(GL_UNIFORM_BUFFER,ubo); glBindBufferBase(GL_UNIFORM_BUFFER,0,ubo);
    GLuint cube; glGenTextures(1,&cube);
    for(int unit : {4,10}) { glActiveTexture(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_CUBE_MAP,cube); }
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    GLuint output=Texture(GL_RGBA32F), fbo; glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    Require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO"); glViewport(0,0,1,1);
    glBindImageTexture(1,output,0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA32F);
    float maxError=0;
    for(int level=0;level<256;level++) {
        unsigned char pixel[]={static_cast<unsigned char>(level),static_cast<unsigned char>(255-level),128,255};
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_CUBE_MAP,cube);
        for(int face=0;face<6;face++) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        for(int path=0;path<3;path++) {
            float frame[52]={};
            if(path<2) std::memcpy(frame,camera,192);
            else {frame[34]=1; frame[47]=1;} // PT camera direction +Z; environment intensity 1
            glBindBuffer(GL_UNIFORM_BUFFER,ubo); glBufferData(GL_UNIFORM_BUFFER,sizeof(frame),frame,GL_DYNAMIC_DRAW);
            glUseProgram(programs[path]);
            if(path<2) glDrawArrays(GL_TRIANGLES,0,3);
            else {glDispatchCompute(1,1,1); glMemoryBarrier(GL_ALL_BARRIER_BITS);}
            float rgba[4]; glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,rgba);
            for(int c=0;c<3;c++) {
                float s=pixel[c]/255.0f;
                float expected=s<=0.04045f?s/12.92f:std::pow((s+0.055f)/1.055f,2.4f);
                maxError=std::max(maxError,std::abs(rgba[c]-expected));
                Require(std::isfinite(rgba[c]) && std::abs(rgba[c]-expected)<0.00001f,"Skybox must decode sRGB exactly once");
            }
        }
    }

    // Real post-TAA compositor: opaque depth must preserve geometry, while
    // fractional coverage must blend the sky even if the nearest depth is occupied.
    auto composite=Program("res/shaders/SkyboxComposite.shader");
    auto source=Texture(GL_RGBA32F), depth=Texture(GL_RGBA32F);
    glActiveTexture(GL_TEXTURE12); glBindTexture(GL_TEXTURE_CUBE_MAP,cube);
    const unsigned char white[]={255,255,255,255};
    for(int face=0;face<6;face++) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
    glBindBuffer(GL_UNIFORM_BUFFER,ubo); glBufferData(GL_UNIFORM_BUFFER,64,camera,GL_DYNAMIC_DRAW);
    glUseProgram(composite);
    for(float coverage : {0.0f,0.25f,0.5f,1.0f}) for(float z : {0.4f,1.0f}) {
        float surface[]={0.2f*coverage,0.4f*coverage,0.6f*coverage,coverage};
        float depths[]={z,z,z,z};
        glActiveTexture(GL_TEXTURE10);glBindTexture(GL_TEXTURE_2D,source);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_FLOAT,surface);
        glActiveTexture(GL_TEXTURE11);glBindTexture(GL_TEXTURE_2D,depth);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_FLOAT,depths);
        glDrawArrays(GL_TRIANGLES,0,3);
        float result[4];glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,result);
        for(int c=0;c<3;c++) Require(std::abs(result[c]-(surface[c]+1-coverage))<1e-6f,"Sky silhouette coverage composition");
        Require(result[3]==1.0f,"Composite alpha");
    }

    // TAA must preserve fractional opacity rather than turning a silhouette
    // opaque before the sky is added. Test both clear and occupied depth.
    auto taa=Program("res/shaders/TAA.shader");
    auto history=Texture(GL_RGBA32F), velocity=Texture(GL_RGBA32F);
    float zero[4]={};
    glActiveTexture(GL_TEXTURE12);glBindTexture(GL_TEXTURE_2D,velocity);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_FLOAT,zero);
    glVertexAttrib2f(1,0.5f,0.5f); glUseProgram(taa);
    for(float coverage : {0.0f,0.25f,1.0f}) for(float z : {0.4f,1.0f}) for(int valid : {0,1}) {
        float frame[36]={};for(int m=0;m<2;m++)for(int i=0;i<4;i++)frame[m*16+i*5]=1;
        std::memcpy(&frame[34],&valid,sizeof(valid));
        glBindBuffer(GL_UNIFORM_BUFFER,ubo);glBufferData(GL_UNIFORM_BUFFER,sizeof(frame),frame,GL_DYNAMIC_DRAW);
        float surface[]={0.2f*coverage,0.4f*coverage,0.6f*coverage,coverage};float depths[]={z,z,z,z};
        for(auto unit : {10,11}) {glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,unit==10?source:history);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_FLOAT,surface);}
        glActiveTexture(GL_TEXTURE13);glBindTexture(GL_TEXTURE_2D,depth);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_FLOAT,depths);
        glDrawArrays(GL_TRIANGLES,0,3);
        float result[4];glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,result);
        for(int c=0;c<4;c++) Require(std::abs(result[c]-surface[c])<1e-6f,"TAA must retain premultiplied coverage");
    }
    std::cout << "PASS: post-TAA sky composition and TAA coverage, with/without history\n";
    Require(glGetError()==GL_NO_ERROR,"GL errors");
    std::cout << "PASS: both raster sky variants + actual PT_Environment; 256 sRGB levels, max error " << maxError << "\n";
    glfwDestroyWindow(window); glfwTerminate(); return 0;
} catch(const std::exception& e) {std::cerr << e.what()<<"\n"; return 1;} }
