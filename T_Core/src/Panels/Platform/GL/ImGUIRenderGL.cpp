#pragma once
#include<Platform/RHI/RHIImGuiRenderer.h>
#ifdef RenderAPI_OpenGL
namespace OpenGL_ImGui
{
	void Init(Engine::Application& app) {

		ImGui::CreateContext();
		ImGui::StyleColorsDark();


		ImGuiIO& io = ImGui::GetIO();

		io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
		io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;//��IMGUI�Ĵ����Ƶ�Main Window���½�����ϵͳ���ڣ�ʹ��GUI_Window�ܹ���ȫ��ʾ��


		GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetWindow());
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 410");
	}

	void Begin(Engine::Application& app, float& m_Time) {
		ImGuiIO& io = ImGui::GetIO();

		io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

		float time = (float)glfwGetTime();
		io.DeltaTime = m_Time > 0.0f ? (time - m_Time) : (1.0f / 60.0f);
		m_Time = time;

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void End() {
		ImGuiIO& io = ImGui::GetIO();
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());


		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}
	}

	void Shutdown() {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	ImTextureID GetTextureID(RHITexture2D* texture)
	{
		return texture
			? static_cast<ImTextureID>(texture->GetNativeID())
			: ImTextureID_Invalid;
	}

	void ReleaseTexture(RHITexture2D* texture)
	{
		(void)texture;
	}
}



struct OpenGLImGuiRegisterer {
	OpenGLImGuiRegisterer() {
		RHIImGUIRenderer::APIFunctions funcs = {
			OpenGL_ImGui::Init,
			OpenGL_ImGui::Begin,
			OpenGL_ImGui::End,
			OpenGL_ImGui::Shutdown,
				OpenGL_ImGui::GetTextureID,
				OpenGL_ImGui::ReleaseTexture
		};
		RHIImGUIRenderer::Register(funcs);
	}
};


static OpenGLImGuiRegisterer s_AutoRegister;
#endif // RenderAPI_OpenGL