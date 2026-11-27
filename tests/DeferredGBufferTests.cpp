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
        GLenum glStage = stage == ShaderStage::Vertex ? GL_VERTEX_SHADER
                       : stage == ShaderStage::Compute ? GL_COMPUTE_SHADER
                       : GL_FRAGMENT_SHADER;
        GLuint shader = glCreateShader(glStage);
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
    for (auto target : {ShaderTarget::Vulkan, ShaderTarget::OpenGL})
        for (const char* path : {
             "res/shaders/GBuffer.shader",
             "res/shaders/GBufferClearCoat.shader",
             "res/shaders/GBufferSubsurface.shader",
             "res/shaders/DeferredLight.shader",
             "res/shaders/DeferredTileClassify.shader",
             "res/shaders/DeferredTileLightUnlit.shader",
             "res/shaders/DeferredTileLightSimple.shader",
             "res/shaders/DeferredTileLightComplex.shader",
             "res/shaders/DeferredDirectionalComplex.shader",
             "res/shaders/DeferredLightVolumeStencil.shader",
             "res/shaders/DeferredLocalLightSimple.shader",
             "res/shaders/DeferredLocalLightComplex.shader",
             "res/shaders/SceneModelPreview.shader",
             "tests/shaders/GBufferProcedural.shader",
             "tests/shaders/GBufferPackedInput.shader"})
            Require(CompileShaderFile(path,target).Success,path);
    // Reject the old four-target ABI, a wrong type, and an extra target.
    std::filesystem::create_directories("build/gbuffer-tests");
    for (int bad = 0; bad < 3; ++bad) {
        const std::string path = "build/gbuffer-tests/invalid.shader";
        std::ofstream file(path);
        file << "#render_pass gbuffer\n#shader vertex\n#version 430 core\nvoid main(){gl_Position=vec4(0); }\n"
                "#shader fragment\n#version 430 core\nlayout(location=0) out vec4 n;\nlayout(location=1) out vec4 c;\n"
             << (bad == 1 ? "layout(location=2) out vec4 mr;\n" : "layout(location=2) out vec2 mr;\n")
             << "layout(location=3) out vec2 v;\n";
        if (bad != 0) file << "layout(location=4) out vec4 e;\n";
        if (bad == 2) file << "layout(location=5) out vec4 extra;\n";
        file << "void main(){n=vec4(1);c=vec4(1);mr=" << (bad==1?"vec4(1)":"vec2(1)") << ";v=vec2(0);";
        if(bad!=0) file << "e=vec4(0);";
        if(bad==2) file << "extra=vec4(0);";
        file << "}";file.close();
        for(auto target : {ShaderTarget::OpenGL,ShaderTarget::Vulkan})
            Require(!CompileShaderFile(path,target).Success,"Invalid GBuffer ABI accepted");
    }
    auto reflection = CompileShaderFile("tests/shaders/GBufferProcedural.shader",ShaderTarget::Vulkan).Reflection;
    Require(reflection.UniformBlocks.empty() && reflection.SampledImages.empty() && reflection.PushConstants.empty(), "Procedural inputs must be optional");
    Require(glfwInit(),"GLFW init"); glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto window=glfwCreateWindow(384,64,"GBuffer tests",nullptr,nullptr); Require(window,"GL context");
    glfwMakeContextCurrent(window); glewExperimental=GL_TRUE; Require(glewInit()==GLEW_OK,"GLEW"); glGetError();
    GLuint procedural=Program("tests/shaders/GBufferProcedural.shader");
    GLuint packed=Program("tests/shaders/GBufferPackedInput.shader");
    GLuint light=Program("res/shaders/DeferredLight.shader");
    const int W=384,H=64;
    GLuint vao; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    float quad[]={-1,-1,0,0, 3,-1,2,0, -1,3,0,2};
    GLuint vb; glGenBuffers(1,&vb); glBindBuffer(GL_ARRAY_BUFFER,vb); glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    for(int i=0;i<2;i++) { glEnableVertexAttribArray(i); glVertexAttribPointer(i,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(i*2*sizeof(float))); }
    GLuint fbo; glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    GLuint textures[5]; GLenum formats[]={GL_RGBA16F,GL_RGBA8,GL_RG16F,GL_RG16F,GL_RGBA16F};
    GLenum attachments[5];
    for(int i=0;i<5;i++) { textures[i]=Texture(formats[i],W,H,i==2||i==3?GL_RG:GL_RGBA); attachments[i]=GL_COLOR_ATTACHMENT0+i; glFramebufferTexture2D(GL_FRAMEBUFFER,attachments[i],GL_TEXTURE_2D,textures[i],0); }
    GLuint depth; glGenTextures(1,&depth); glBindTexture(GL_TEXTURE_2D,depth);
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH24_STENCIL8,W,H,0,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,depth,0);
    glDrawBuffers(5,attachments); Require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"GBuffer framebuffer");
    std::array<float,6*3*4> palette{};
    for(int i=0;i<6;i++) { int c=i*4,m=(6+i)*4,e=(12+i)*4;
        palette[c]=0.8f; palette[c+1]=0.2f; palette[c+2]=0.05f; palette[c+3]=1;
        palette[m]=i==2||i==3?1.f:0.f; palette[m+1]=i==1||i==3?0.7f:0.15f; palette[m+2]=i==5?1.f:0.f;
        if(i==4) {palette[e]=3;palette[e+1]=0.5f;palette[e+2]=0.1f;}
    }
    GLuint pal=Texture(GL_RGBA32F,6,3,GL_RGBA,palette.data());
    GLuint output=Texture(GL_RGBA16F,W,H), outFbo; glGenFramebuffers(1,&outFbo); glBindFramebuffer(GL_FRAMEBUFFER,outFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0); glDrawBuffer(GL_COLOR_ATTACHMENT0);
    Require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Light framebuffer");
    std::array<float,48> ubo{}; for(int i=0;i<4;i++) {ubo[i*5]=1;ubo[16+i*5]=1;} ubo[34]=3; ubo[36]=W; ubo[37]=H;
    GLuint ub,ssbo; glGenBuffers(1,&ub); glGenBuffers(1,&ssbo);
    float lamp[]={0,0,-1,0, 1,1,1,2, 0,0,0,0, 0,0,0,0};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,ssbo);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(lamp),lamp,GL_STATIC_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,ssbo);
    auto render=[&](GLuint shader,bool lights) {
        glViewport(0,0,W,H); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glDrawBuffers(5,attachments);
        glClearColor(0,0,0,0); glClearDepth(1); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); glUseProgram(shader);
        glActiveTexture(GL_TEXTURE17); glBindTexture(GL_TEXTURE_2D,pal); glDrawArrays(GL_TRIANGLES,0,3);
        glBindFramebuffer(GL_FRAMEBUFFER,outFbo); glDisable(GL_DEPTH_TEST); glClear(GL_COLOR_BUFFER_BIT);
        const GLuint inputs[]={textures[0],textures[1],textures[2],depth,textures[4],textures[3]};
        for(int i=0;i<6;i++) {glActiveTexture(GL_TEXTURE10+i);glBindTexture(GL_TEXTURE_2D,inputs[i]);}
        ubo[42]=lights?1.f:0.f;
        glBindBuffer(GL_UNIFORM_BUFFER,ub);glBufferData(GL_UNIFORM_BUFFER,sizeof(ubo),ubo.data(),GL_DYNAMIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,0,ub);
        glUseProgram(light);glDrawArrays(GL_TRIANGLES,0,3);
        std::vector<float> pixels(W*H*4);glReadPixels(0,0,W,H,GL_RGBA,GL_FLOAT,pixels.data());
        Require(glGetError()==GL_NO_ERROR,"GL draw error");return pixels;
    };
    auto a=render(procedural,true), b=render(packed,true), dark=render(procedural,false);
    float maxError=0;for(size_t i=0;i<a.size();i++) {Require(std::isfinite(a[i]),"Non-finite PBR");maxError=std::max(maxError,std::abs(a[i]-b[i]));}
    Require(maxError<0.001f,"Different shader inputs produced different lighting");
    auto at=[&](int tile,int channel){return (32*W+tile*64+32)*4+channel;};
    Require(std::abs(dark[at(4,0)]-3.f)<0.01f,"HDR emissive disappeared without lights");
    Require(dark[at(0,0)]==0,"Unlit scene received unexpected lighting");
    for(int c=0;c<3;c++) Require(std::abs(a[at(5,c)]-dark[at(5,c)])<0.001f,"Unlit depends on lights");
    Require(std::abs(a[at(0,0)]-a[at(1,0)])>0.01f,"Roughness has no effect");
    Require(std::abs(a[at(1,0)]-a[at(3,0)])>0.01f,"Metallic has no effect");
    std::ofstream ppm("build/gbuffer-acceptance.ppm",std::ios::binary);ppm<<"P6\n"<<W<<" "<<H*3<<"\n255\n";
    for(const auto* pixels : {&a,&b,&dark}) for(int y=H-1;y>=0;y--) for(int x=0;x<W;x++) for(int c=0;c<3;c++) {
        float v=(*pixels)[(y*W+x)*4+c]; unsigned char byte=(unsigned char)(255*std::pow(v/(1+v),1/2.2f));ppm.write((char*)&byte,1);
    }
    std::cout<<"PASS: both backends compile; GPU input equivalence max error="<<maxError<<"; metal/roughness/emissive/unlit\n";

    // Test UE-style Compute Tile Classification + DispatchIndirect equivalence against Uber shader
    // across Simple (DefaultLit), Complex (ClearCoat & Subsurface), Unlit, and Sky tiles.
    {
        const std::string complexShaderPath = "build/gbuffer-tests/complex_tiles.shader";
        {
            std::ofstream f(complexShaderPath);
            f << "#render_pass gbuffer\n"
              << "#shader vertex\n#version 430 core\nlayout(location=0) in vec2 position;\n"
              << "void main(){ gl_Position = vec4(position, 0.0, 1.0); }\n"
              << "#shader fragment\n#version 430 core\n#define TS_GBUFFER_WRITE\n"
              << "#include \"../../res/shaders/include/GBuffer.glsl\"\n"
              << "void main() {\n"
              << "    int tile = int(gl_FragCoord.x) / 64;\n"
              << "    if (tile == 5) discard;\n" // Pure sky column
              << "    GBufferSurface s = DefaultGBufferSurface();\n"
              << "    s.normal = normalize(vec3(0.2, 0.3, 0.93));\n"
              << "    s.baseColor = vec3(0.75, 0.25, 0.15);\n"
              << "    s.roughness = 0.5;\n"
              << "    if (tile == 0) { s.shadingModel = TS_DEFAULT_LIT; }\n"
              << "    else if (tile == 1) { s.shadingModel = TS_CLEAR_COAT; }\n"
              << "    else if (tile == 2) { s.shadingModel = TS_SUBSURFACE; s.normal = normalize(vec3(0.96, 0.1, 0.15)); }\n"
              << "    else if (tile == 3) { s.shadingModel = TS_UNLIT; s.emissive = vec3(0.2, 0.4, 0.6); }\n"
              << "    else { s.shadingModel = TS_DEFAULT_LIT; s.normal = normalize(vec3(0.96, 0.1, 0.15)); }\n"
              << "    WriteGBuffer(s);\n"
              << "}\n";
        }
        GLuint complexGbuf = Program(complexShaderPath.c_str());
        GLuint classifyProg = Program("res/shaders/DeferredTileClassify.shader");
        GLuint tileUnlitProg = Program("res/shaders/DeferredTileLightUnlit.shader");
        GLuint tileSimpleProg = Program("res/shaders/DeferredTileLightSimple.shader");
        GLuint tileComplexProg = Program("res/shaders/DeferredTileLightComplex.shader");

        auto uberPixels = render(complexGbuf, true);

        // Now run Compute Tile Classification + DispatchIndirect on the same GBuffer.
        GLuint tileOut = Texture(GL_RGBA16F, W, H);
        const uint32_t tilesX = (W + 7) / 8, tilesY = (H + 7) / 8;
        const uint32_t totalTiles = tilesX * tilesY;
        uint32_t classifyUboData[4] = { (uint32_t)W, (uint32_t)H, tilesX, tilesY };
        uint32_t initArgs[16] = { 0,1,1,0,  0,1,1,0,  0,1,1,0,  0,totalTiles,0,0 };
        GLuint classifyUbo, argsBuf, simpleBuf, complexBuf, unlitBuf;
        glGenBuffers(1, &classifyUbo); glBindBuffer(GL_UNIFORM_BUFFER, classifyUbo);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(classifyUboData), classifyUboData, GL_DYNAMIC_DRAW);
        glGenBuffers(1, &argsBuf); glBindBuffer(GL_SHADER_STORAGE_BUFFER, argsBuf);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(initArgs), initArgs, GL_DYNAMIC_DRAW);
        auto makeTileBuf = [&](GLuint& b) {
            glGenBuffers(1, &b); glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
            glBufferData(GL_SHADER_STORAGE_BUFFER, totalTiles * sizeof(uint32_t), nullptr, GL_DYNAMIC_COPY);
        };
        makeTileBuf(simpleBuf); makeTileBuf(complexBuf); makeTileBuf(unlitBuf);

        const GLuint inputs[] = { textures[0], textures[1], textures[2], depth, textures[4], textures[3] };
        for (int i = 0; i < 6; ++i) { glActiveTexture(GL_TEXTURE10 + i); glBindTexture(GL_TEXTURE_2D, inputs[i]); }
        glBindImageTexture(5, tileOut, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, classifyUbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, argsBuf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, simpleBuf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, complexBuf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, unlitBuf);
        glUseProgram(classifyProg);
        glDispatchCompute(tilesX, tilesY, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        uint32_t readArgs[16] = {};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, argsBuf);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(readArgs), readArgs);
        Require(readArgs[0] > 0, "Expected non-zero Simple tiles");
        Require(readArgs[4] > 0, "Expected non-zero Complex tiles");
        Require(readArgs[8] > 0, "Expected non-zero Unlit tiles");
        Require(readArgs[12] > 0, "Expected non-zero skipped Sky tiles");
        Require(readArgs[0] + readArgs[4] + readArgs[8] + readArgs[12] == totalTiles, "Tile counts must sum to totalTiles");

        ubo[42] = 1.0f;
        glBindBuffer(GL_UNIFORM_BUFFER, ub);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(ubo), ubo.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, ub);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, argsBuf);
        glUseProgram(tileUnlitProg);   glDispatchComputeIndirect(32);
        glUseProgram(tileSimpleProg);  glDispatchComputeIndirect(0);
        glUseProgram(tileComplexProg); glDispatchComputeIndirect(16);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);

        GLuint readFbo; glGenFramebuffers(1, &readFbo); glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tileOut, 0);
        std::vector<float> tieredPixels(W * H * 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, tieredPixels.data());
        Require(glGetError() == GL_NO_ERROR, "GL compute tile dispatch error");

        float tieredMaxErr = 0.0f;
        for (size_t i = 0; i < uberPixels.size(); ++i)
        {
            Require(std::isfinite(tieredPixels[i]), "Non-finite tiered lighting");
            tieredMaxErr = std::max(tieredMaxErr, std::abs(uberPixels[i] - tieredPixels[i]));
        }
        Require(tieredMaxErr < 0.001f, "Uber vs UE Tiered lighting mismatch");
        // Verify ClearCoat (tile 1) differs from DefaultLit (tile 0), and grazing Subsurface (tile 2)
        // differs from grazing DefaultLit (tile 4).
        Require(std::abs(tieredPixels[at(1, 0)] - tieredPixels[at(0, 0)]) > 0.01f, "ClearCoat matches DefaultLit");
        Require(std::abs(tieredPixels[at(2, 0)] - tieredPixels[at(4, 0)]) > 0.005f, "Subsurface matches DefaultLit");
        std::cout << "PASS: Uber vs UE Compute Tile Classification max error=" << tieredMaxErr
                  << " (simple=" << readArgs[0] << ", complex=" << readArgs[4]
                  << ", unlit=" << readArgs[8] << ", skySkipped=" << readArgs[12] << ")\n";
    }
    glfwDestroyWindow(window);glfwTerminate();return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
