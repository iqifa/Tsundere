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
struct LightData { glm::vec4 direction, color, position, params; };
static glm::vec3 GoldenBRDF(glm::vec3 base,float metal,float rough,glm::vec3 L) {
    const double pi=3.141592653589793;
    const glm::dvec3 N(0,0,1), V(0,0,1), H=glm::normalize(glm::dvec3(L)+V);
    const double nl=glm::dot(N,glm::dvec3(L)), nh=glm::dot(N,H), vh=glm::dot(V,H);
    if(nl<=0) return glm::vec3(0);
    double a=std::max(double(rough)*rough,0.0025), a2=a*a, d=(1-nh*nh)+nh*nh*a2;
    double D=a2/(pi*d*d), G=2*nl/(nl+std::sqrt(a2+(1-a2)*nl*nl));
    glm::dvec3 f0=glm::mix(glm::dvec3(0.04),glm::dvec3(base),double(metal));
    glm::dvec3 F=f0+(1.0-f0)*std::pow(1-vh,5);
    return glm::vec3((1.0-F)*glm::dvec3(base)*(1.0-double(metal))*(nl/pi)+D*G*F/4.0);
}
static void Close(glm::vec3 a,glm::vec3 b,float relative,const char* label) {
    for(int c=0;c<3;c++) if(!std::isfinite(a[c]) || !std::isfinite(b[c]) ||
        std::abs(a[c]-b[c])>0.0005f+relative*std::max(std::abs(a[c]),std::abs(b[c]))) {
        std::cerr<<label<<": "<<a[c]<<" vs "<<b[c]<<" channel "<<c<<"\n";
        throw std::runtime_error(label);
    }
}
int main() { try {
    Engine::Log::Init();
    for(auto target:{ShaderTarget::OpenGL,ShaderTarget::Vulkan})
        for(auto path:{"res/shaders/MaterialTest.shader","res/shaders/SceneModelPreview.shader",
                      "res/shaders/DeferredLight.shader","tests/shaders/DirectLightingPT.shader"})
            Require(CompileShaderFile(path,target).Success,path);
    Require(glfwInit(),"GLFW");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,5);
    auto window=glfwCreateWindow(4,4,"lighting test",nullptr,nullptr);Require(window,"GL context");
    glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;Require(glewInit()==GLEW_OK,"GLEW");glGetError();
    GLuint forward=Program("res/shaders/MaterialTest.shader"), deferred=Program("res/shaders/DeferredLight.shader");
    GLuint pt=Program("tests/shaders/DirectLightingPT.shader");
    GLuint vao,vbo;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    float vertices[]={-1,-1,0,3,-1,0,-1,3,0};
    glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,nullptr);glVertexAttrib2f(2,0.5f,0.5f);
    glm::mat4 identity(1);
    glUseProgram(forward);
    glUniformMatrix4fv(glGetUniformLocation(forward,"pc.model"),1,GL_FALSE,&identity[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(forward,"pc.prevModel"),1,GL_FALSE,&identity[0][0]);
    GLuint buffers[7];glGenBuffers(7,buffers);
    auto upload=[&](int i,GLenum target,int binding,const void* data,size_t size) {
        glBindBuffer(target,buffers[i]);glBufferData(target,size,data,GL_DYNAMIC_DRAW);glBindBufferBase(target,binding,buffers[i]);
    };
    glm::mat4 camera[]={identity,identity,identity};upload(0,GL_UNIFORM_BUFFER,2,camera,sizeof(camera));
    std::array<float,40> frame{};frame[14]=1;upload(1,GL_UNIFORM_BUFFER,1,frame.data(),sizeof(frame));
    GLuint fbo;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    GLuint color=Texture(GL_RGBA32F),motion=Texture(GL_RGBA32F);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,motion,0);
    GLenum outputs[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(2,outputs);
    Require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO");glViewport(0,0,1,1);
    const unsigned char whiteBytes[]={255,255,255,255};GLuint white=Texture(GL_RGBA8,1,1,whiteBytes);
    GLuint inputs[6];for(auto& t:inputs)t=Texture(GL_RGBA32F);
    auto setTexture=[&](GLuint t,glm::vec4 v,int unit) {
        glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,t);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_FLOAT,&v[0]);
    };
    glm::vec3 base(0.6f,0.2f,0.08f);
    std::vector<LightData> lights;
    auto raster=[&](bool isForward,float metallic,float rough) {
        upload(3,GL_SHADER_STORAGE_BUFFER,isForward?4:1,lights.data(),lights.size()*sizeof(LightData));
        if(isForward) {
            glVertexAttrib3f(1,0,0,1);
            std::array<float,32> mat{};mat[0]=base.r;mat[1]=base.g;mat[2]=base.b;mat[3]=1;
            mat[8]=mat[9]=1;mat[12]=rough;mat[13]=metallic;
            upload(2,GL_UNIFORM_BUFFER,3,mat.data(),sizeof(mat));
            glActiveTexture(GL_TEXTURE10);glBindTexture(GL_TEXTURE_2D,white);glUseProgram(forward);
        } else {
            glVertexAttrib2f(1,0.5f,0.5f);
            setTexture(inputs[0],{0,0,1,0},10);setTexture(inputs[1],glm::vec4(base,1),11);
            setTexture(inputs[2],{metallic,rough,0,0},12);setTexture(inputs[3],glm::vec4(0.5f),13);
            setTexture(inputs[4],glm::vec4(0),14);setTexture(inputs[5],glm::vec4(0),15);
            std::array<float,48> data{};std::memcpy(data.data(),&identity,sizeof(identity));
            data[34]=1;data[42]=float(lights.size());upload(4,GL_UNIFORM_BUFFER,0,data.data(),sizeof(data));glUseProgram(deferred);
        }
        glDrawArrays(GL_TRIANGLES,0,3);glm::vec4 c;glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,&c[0]);
        return glm::vec3(c);
    };
    auto trace=[&](float metallic,float rough,uint32_t samples,bool bsdf=false,bool blocked=false,bool shadowed=false) {
        std::array<uint32_t,52> data{};data[40]=uint32_t(lights.size());upload(4,GL_UNIFORM_BUFFER,0,data.data(),sizeof(data));
        struct TestSurface {glm::vec4 baseMetal,positionRough,normal,view;glm::uvec4 config;};
        TestSurface test{glm::vec4(base,metallic),glm::vec4(0,0,0,rough),glm::vec4(0,0,1,0),glm::vec4(0,0,1,0),
            glm::uvec4(samples,bsdf,blocked,shadowed)};
        upload(5,GL_UNIFORM_BUFFER,6,&test,sizeof(test));
        upload(3,GL_SHADER_STORAGE_BUFFER,3,lights.data(),lights.size()*sizeof(LightData));
        upload(6,GL_SHADER_STORAGE_BUFFER,7,nullptr,samples*sizeof(glm::vec4));
        glUseProgram(pt);glDispatchCompute((samples+63)/64,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        std::vector<glm::vec4> values(samples);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[6]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,samples*sizeof(glm::vec4),values.data());
        glm::dvec3 sum(0);for(auto v:values) {Require(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z),"PT NaN/Inf");sum+=glm::dvec3(v);}
        return glm::vec3(sum/double(samples));
    };
    LightData directional{{0,0,-1,0},{1,0.7f,0.3f,2},{0,0,0,0},{0,0,0,0}};
    LightData point{{0,0,-1,0},{1,0.7f,0.3f,2},{0.4f,0.2f,2,1},{8,2,15,30}};
    LightData spot=point;spot.position.w=2;
    LightData area{{0,0,-1,0},{1,0.7f,0.3f,2},{0.2f,0.1f,2,3},{2,1.5f,0,0}};
    LightData disk=area;disk.params.z=1;
    int cases=0;float maxPunctualError=0,maxAreaError=0;
    for(auto light:{directional,point,spot,area,disk})
    for(float rough:{0.0f,0.2f,0.5f,1.0f})
    for(float metallic:{0.0f,0.03f,0.499f,0.501f,1.0f}) {
        lights={light};auto a=raster(true,metallic,rough),b=raster(false,metallic,rough);
        auto c=trace(metallic,rough,light.position.w==3?65536:1);
        Close(a,b,0.0001f,"Forward/deferred");Close(a,c,light.position.w==3?0.04f:0.0002f,"Raster/PT direct");
        for(int ch=0;ch<3;ch++) {
            float e=std::abs(a[ch]-c[ch])/std::max(0.01f,std::max(a[ch],c[ch]));
            if(light.position.w==3)maxAreaError=std::max(maxAreaError,e);else maxPunctualError=std::max(maxPunctualError,e);
        }
        if(light.position.w==0) Close(a,GoldenBRDF(base,metallic,rough,{0,0,1})*glm::vec3(light.color)*light.color.w,0.0003f,"CPU GGX reference");
        ++cases;
    }
    lights={directional};auto lo=trace(0.499f,0.5f,1),hi=trace(0.501f,0.5f,1);
    Close(lo,hi,0.01f,"Metallic threshold discontinuity");
    Close(trace(0,0.5f,1,false,true,true),glm::vec3(0),0,"PT shadow blocker ignored");
    Close(trace(0,0.5f,1,false,true,false),raster(true,0,0.5f),0.0002f,"Unoccluded direct view");
    // Finite mixture weights, plus a high-resolution independent hemisphere integral.
    for(float metallic:{0.0f,0.5f,1.0f}) {
        glm::dvec3 expected(0);const int n=512;
        for(int i=0;i<n;i++) {double z=(i+0.5)/n;
            expected+=glm::dvec3(GoldenBRDF(base,metallic,0.5f,glm::vec3(std::sqrt(1-z*z),0,z)))*(2.0*3.141592653589793/n);}
        Close(trace(metallic,0.5f,262144,true),glm::vec3(expected),0.015f,"BSDF mixture PDF/energy");
    }
    for(auto invalid:{0,1,2,3,4}) {
        auto light=invalid==0?directional:invalid==1?point:invalid==2?spot:area;
        if(invalid==0||invalid==2)light.direction=glm::vec4(0);
        if(invalid==1||invalid==3)light.params.x=0;
        if(invalid==4)light.direction.z=1;
        lights={light};Close(raster(true,0,0.5f),glm::vec3(0),0,"Invalid/back-facing light");
        Close(trace(0,0.5f,64),glm::vec3(0),0,"PT invalid/back-facing light");
    }
    spot.params.z=spot.params.w=30;lights={spot};Close(raster(true,0.2f,0.5f),trace(0.2f,0.5f,1),0.0002f,"Equal spot angles");
    glm::vec3 sum(0);for(auto light:{directional,point,spot,area}){lights={light};sum+=raster(true,0.3f,0.5f);}
    lights={directional,point,spot,area};Close(raster(true,0.3f,0.5f),sum,0.0002f,"Mixed light sum");
    Close(trace(0.3f,0.5f,65536),sum,0.02f,"Mixed PT direct sum");
    lights={directional};auto singleDirectional=raster(true,0,0.5f);
    lights={directional,directional};Close(raster(true,0,0.5f),2.0f*singleDirectional,0.0002f,"Multiple directional lights");
    Close(trace(0,0.5f,1),2.0f*singleDirectional,0.0002f,"PT multiple directional lights");
    lights={directional};lights[0].color.w=100;
    GLuint hdr=Texture(GL_RGBA16F);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,hdr,0);
    auto bright=raster(true,0,0.5f);Require(bright.x>1,"Forward HDR clipped");Close(bright,trace(0,0.5f,1),0.001f,"Forward HDR precision");
    Require(glGetError()==GL_NO_ERROR,"GL error");
    std::cout<<"PASS: "<<cases<<" direct-light cases; max relative punctual error="<<maxPunctualError
        <<", area error="<<maxAreaError<<"; GL/Vulkan compile; GGX CPU reference, area MIS, metallic continuity, BSDF PDF, shadows, HDR\n";
    glfwDestroyWindow(window);glfwTerminate();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;} }
