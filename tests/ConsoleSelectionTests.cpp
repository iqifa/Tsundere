#include "Panels/ConsolePanel.h"
#include "imgui/imgui_internal.h"
#include <cassert>
#include <iostream>
#include <fstream>

static std::string clipboard;
int main() {
 Engine::Log::Init();
 ImGui::CreateContext();
 auto& io=ImGui::GetIO(); io.DisplaySize={1000,700}; io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
 unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
 ImGui::GetPlatformIO().Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text){clipboard=text;};
 ImGui::GetPlatformIO().Platform_GetClipboardTextFn=[](ImGuiContext*){return clipboard.c_str();};
 Engine::Log::GetClientLogger()->set_pattern("%v");
 Engine::Log::ClearClientConsole();
 Engine::Log::GetClientLogger()->info("alpha中文 omega");
 Engine::Log::GetClientLogger()->warn("second line");
 EditorWindows::Console()=true;
 ConsolePanel panel;
 auto frame=[&] {
  ImGui::NewFrame(); ImGui::SetNextWindowPos({10,10}); ImGui::SetNextWindowSize({900,600});
  panel.OnImGuiRender(); ImGui::Render();
 };
 frame(); frame(); frame();
 ImGuiWindow* child=nullptr;
 for(auto* window:ImGui::GetCurrentContext()->Windows)
  if(std::string(window->Name).find("ConsoleScroll")!=std::string::npos)child=window;
 assert(child);
 auto width=[&](const char* text){return io.FontDefault ? io.FontDefault->CalcTextSizeA(13,FLT_MAX,0,text).x : ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(),FLT_MAX,0,text).x;};
 const auto start=child->DC.CursorStartPos;
 const float row=ImGui::GetTextLineHeightWithSpacing();
 auto drag=[&](float x0,float y0,float x1,float y1) {
  io.AddMousePosEvent(x0,y0); frame(); io.AddMouseButtonEvent(0,true); frame();
  io.AddMousePosEvent(x1,y1); frame(); io.AddMouseButtonEvent(0,false); frame();
 };
 auto copy=[&] {
  io.AddKeyEvent(ImGuiMod_Ctrl,true); io.AddKeyEvent(ImGuiKey_C,true); frame();
  io.AddKeyEvent(ImGuiKey_C,false); io.AddKeyEvent(ImGuiMod_Ctrl,false); frame();
 };
 drag(start.x+width("alpha"),start.y+3,start.x+width("sec"),start.y+row+3);
 copy(); assert(clipboard=="中文 omega\nsec");
 Engine::Log::GetClientLogger()->info("new entry"); frame(); copy();
 assert(clipboard=="中文 omega\nsec");
 drag(start.x+width("alpha中文"),start.y+3,start.x+width("alpha"),start.y+3);
 copy(); assert(clipboard=="中文");
 io.AddKeyEvent(ImGuiMod_Ctrl,true); io.AddKeyEvent(ImGuiKey_A,true); frame();
 io.AddKeyEvent(ImGuiKey_A,false); io.AddKeyEvent(ImGuiMod_Ctrl,false); frame(); copy();
 assert(clipboard=="alpha中文 omega\nsecond line\nnew entry\n");
 // Ring-buffer eviction must discard the selection, never reinterpret its offsets.
 for(int i=0;i<1001;++i)Engine::Log::GetClientLogger()->info("replacement {}",i);
 frame(); clipboard="unchanged"; copy(); assert(clipboard=="unchanged");
 ImGui::DestroyContext();
 std::ofstream("build/console-selection-tests.passed") << "PASS\n";
 std::cout<<"PASS: direct drag, UTF-8, multiline, reverse selection, Ctrl+C/A, append and eviction\n";
}
