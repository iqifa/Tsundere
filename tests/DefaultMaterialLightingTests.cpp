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
    for(auto target:{ShaderTarget::OpenGL,ShaderTarget::Vulkan})
        {
        auto compiled = CompileShaderFile("res/shaders/SceneModelPreview.shader", target);
        Require(compiled.Success,"Default shader compilation");
        const auto* block = compiled.Reflection.FindStorageBlock(4);
        Require(block && block->Name == "ForwardLocalLights" && block->Members.size() == 1 &&
            block->Members[0].Offset == 0 && block->Members[0].ArrayStride == 64, "Local lights descriptor ABI");
    }
    Require(glfwInit(),"GLFW init"); glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(16,16,"Default lighting test",nullptr,nullptr); Require(window,"GL context");
    glfwMakeContextCurrent(window); glewExperimental=GL_TRUE; Require(glewInit()==GLEW_OK,"GLEW"); glGetError();
    GLuint program=Program("res/shaders/SceneModelPreview.shader"); glUseProgram(program);
    GLuint vao,vbo; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    float vertices[]={-1,-1,0, 3,-1,0, -1,3,0};
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr);
    glVertexAttrib3f(1,0,0,1);
    float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUniformMatrix4fv(glGetUniformLocation(program,"pc.model"),1,GL_FALSE,identity);
    glUniformMatrix4fv(glGetUniformLocation(program,"pc.prevModel"),1,GL_FALSE,identity);
    float camera[48]; for(int i=0;i<48;++i)camera[i]=identity[i%16];
    GLuint buffers[3]; glGenBuffers(3,buffers);
    GLuint lightBuffer; glGenBuffers(1,&lightBuffer);
    auto uploadLights=[&](std::vector<float> data) {
        if(data.empty())data.resize(16,0);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,lightBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,data.size()*sizeof(float),data.data(),GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,lightBuffer);
    };
    uploadLights({});
    auto upload=[&](int index,int binding,const void* data,size_t size) {
        glBindBuffer(GL_UNIFORM_BUFFER,buffers[index]); glBufferData(GL_UNIFORM_BUFFER,size,data,GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER,binding,buffers[index]);
    };
    upload(0,2,camera,sizeof(camera));
    const float base[]={0.4f,0.2f,0.1f,1}; upload(1,3,base,sizeof(base));
    GLuint fbo; glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    GLuint color=Texture(GL_RGBA32F,16,16), velocity=Texture(GL_RG16F,16,16,GL_RG);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,velocity,0);
    GLenum outputs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1}; glDrawBuffers(2,outputs);
    Require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Framebuffer");
    glViewport(0,0,16,16);
    auto render=[&](float intensity,float direction,float ambient,bool redOnly=false) {
        std::array<float,40> frame{}; frame[2]=direction;
        frame[4]=intensity; frame[5]=frame[6]=redOnly?0:intensity; frame[8]=ambient;
        upload(2,1,frame.data(),sizeof(frame));
        glDrawArrays(GL_TRIANGLES,0,3);
        std::array<float,4> pixel{}; glReadBuffer(GL_COLOR_ATTACHMENT0); glReadPixels(8,8,1,1,GL_RGBA,GL_FLOAT,pixel.data());
        Require(glGetError()==GL_NO_ERROR,"Draw/readback error"); return pixel;
    };
    auto lit=render(1,-1,0), bright=render(2,-1,0), off=render(0,-1,0.5f);
    auto back=render(1,1,0), ambient=render(1,1,0.25f), red=render(1,-1,0,true), zero=render(1,0,0);
    for(int c=0;c<3;++c) {
        Require(std::abs(lit[c]-base[c])<0.001f,"Default base color lost");
        Require(std::abs(bright[c]-2*lit[c])<0.001f,"Scene intensity ignored");
        Require(off[c]==0 && back[c]==0 && zero[c]==0,"Fixed preview light still present");
        Require(std::abs(ambient[c]-base[c]*0.25f)<0.001f,"Scene ambient ignored");
    }
    Require(red[0]>0 && red[1]==0 && red[2]==0,"Scene light color ignored");
    // Pixel (8,8) maps to world (0.0625,0.0625,0) with the identity camera.
    std::vector<float> lamp={0,0,-1,0, 1,0.5f,0.25f,2, 0.0625f,0.0625f,1,1, 5,1,15,30};
    uploadLights(lamp); auto point=render(0,-1,0);
    Require(point[0]>0 && point[1]>0 && point[2]>0,"Point light missing");
    Require(std::abs(point[1]/point[0]-0.25f)<0.001f,"Point color/base color incorrect");
    lamp[10]=2; uploadLights(lamp); auto farPoint=render(0,-1,0);
    Require(farPoint[0]<point[0],"Point distance attenuation ignored");
    lamp[10]=1; lamp[13]=3; uploadLights(lamp); auto falloff=render(0,-1,0);
    Require(falloff[0]<point[0],"Falloff ignored");
    lamp[13]=1; lamp[12]=0.5f; uploadLights(lamp);
    Require(render(0,-1,0)[0]==0,"Point radius ignored");
    lamp[12]=0; uploadLights(lamp); Require(render(0,-1,0)[0]==0,"Zero radius produced light");
    lamp[12]=5; lamp[11]=2; uploadLights(lamp); auto spot=render(0,-1,0);
    Require(std::abs(spot[0]-point[0])<0.001f,"Spot center incorrect");
    lamp[0]=std::sin(22.5f*3.14159265f/180); lamp[2]=-std::cos(22.5f*3.14159265f/180);
    uploadLights(lamp); auto penumbra=render(0,-1,0);
    Require(penumbra[0]>0 && penumbra[0]<spot[0],"Spot inner/outer soft edge ignored");
    lamp[0]=1; lamp[2]=0; uploadLights(lamp); Require(render(0,-1,0)[0]==0,"Spot outside cone lit");
    lamp[0]=0; uploadLights(lamp); Require(render(0,-1,0)[0]==0,"Zero spot direction is invalid");
    lamp[2]=-1; lamp[14]=lamp[15]=20; uploadLights(lamp);
    Require(std::abs(render(0,-1,0)[0]-spot[0])<0.001f,"Equal spot angles broken");
    auto two=lamp; two.insert(two.end(),lamp.begin(),lamp.end()); uploadLights(two);
    Require(std::abs(render(0,-1,0)[0]-2*spot[0])<0.001f,"Multiple local lights did not add");
    uploadLights(lamp); Require(std::abs(render(0,-1,0)[0]-spot[0])<0.001f,"Removed light remained in buffer");
    lamp[7]=0; uploadLights(lamp); Require(render(0,-1,0)[0]==0,"Zero light intensity ignored");
    uploadLights({}); Require(render(0,-1,0)[0]==0,"Empty light list retained lighting");
    std::vector<float> area={0,0,-1,0, 1,0.5f,0.25f,2, 0.0625f,0.0625f,1,3, 1,1,0,0};
    uploadLights(area); auto rectangle=render(0,-1,0);
    Require(rectangle[0]>0 && std::isfinite(rectangle[0]),"Rectangle area light missing");
    Require(std::abs(rectangle[1]/rectangle[0]-0.25f)<0.001f,"Area light color incorrect");
    area[12]=2; uploadLights(area); auto wide=render(0,-1,0);
    Require(wide[0]>rectangle[0],"Rectangle width ignored");
    area[12]=1; area[13]=2; uploadLights(area);
    Require(render(0,-1,0)[0]>rectangle[0],"Rectangle height ignored");
    area[13]=1; area[10]=2; uploadLights(area);
    Require(render(0,-1,0)[0]<rectangle[0],"Area distance ignored");
    area[10]=1; area[2]=1; uploadLights(area); Require(render(0,-1,0)[0]==0,"Area backface emits light");
    area[2]=0; uploadLights(area); Require(render(0,-1,0)[0]==0,"Zero area direction invalid");
    area[2]=-1; area[14]=1; uploadLights(area); auto disk=render(0,-1,0);
    Require(disk[0]>0 && disk[0]<rectangle[0],"Disk shape ignored");
    area[12]=2; uploadLights(area); Require(render(0,-1,0)[0]>disk[0],"Disk diameter ignored");
    area[12]=1; area[13]=0; uploadLights(area);
    Require(std::abs(render(0,-1,0)[0]-disk[0])<0.001f,"Disk incorrectly depends on height");
    area[14]=0; uploadLights(area); Require(render(0,-1,0)[0]==0,"Zero rectangle height emits light");
    area[13]=1; area[7]=4; uploadLights(area);
    Require(std::abs(render(0,-1,0)[0]-2*rectangle[0])<0.001f,"Area intensity ignored");
    area[7]=2; lamp[7]=2; lamp[11]=1;
    auto mixed=area; mixed.insert(mixed.end(),lamp.begin(),lamp.end());
    lamp[11]=2; mixed.insert(mixed.end(),lamp.begin(),lamp.end()); uploadLights(mixed);
    auto allTypes=render(1,-1,0);
    for(int c=0;c<3;++c)
        Require(std::abs(allTypes[c]-(lit[c]+rectangle[c]+2*point[c]))<0.001f,"Four light types did not add");
    area[7]=0; uploadLights(area); Require(render(0,-1,0)[0]==0,"Disabled area intensity ignored");
    uploadLights({}); Require(render(0,-1,0)[0]==0,"Removed area left lighting");
    glDeleteBuffers(1,&lightBuffer);
    glDeleteBuffers(3,buffers); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
    glDeleteTextures(1,&color); glDeleteTextures(1,&velocity); glDeleteFramebuffers(1,&fbo); glDeleteProgram(program);
    glfwDestroyWindow(window); glfwTerminate();
    std::cout << "PASS: GL/Vulkan compile; all four light types, rectangle/disk, size/direction/distance/intensity, mixed lights, removal\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
