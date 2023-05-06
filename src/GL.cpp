#pragma once
#include"GLHead.h"
#include"HeadLine.h"

#include"Test/TestHead.h"
#include"Trans/SceneCamera.h"

#include"Scene/Entity.h"

#include"Panels/ImGuiLayer.h"

float  lastX = 540, lastY = 480;

class SceneCamera;

void mouse_callback(GLFWwindow* window, double xpos, double ypos);

void mouse_scrollback(GLFWwindow* window, double xpos, double ypos);

int main(void)
{
	GLFWwindow* window;

	/* Initialize the library */
	if (!glfwInit())
		return -1;

	/* Create a windowed mode window and its OpenGL context */
	window = glfwCreateWindow(1080, 960, "Hello World", NULL, NULL);
	if (!window)
	{
		glfwTerminate();
		return -1;
	}

	/* Make the window's context current */
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	if (glewInit() != GLEW_OK)
	{
		cout << "Error!" << endl;
	}

	cout << glGetString(GL_VERSION) << endl;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	ImGui::StyleColorsDark();


	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	glBlendFunc(GL_SRC0_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetScrollCallback(window, mouse_scrollback); 


	Renderer renderer;

	const char* glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);


	vector<string> texpaths{
		"res/texture/CubeMap/right.jpg",
		"res/texture/CubeMap/left.jpg",
		"res/texture/CubeMap/top.jpg",
		"res/texture/CubeMap/bottom.jpg",
		"res/texture/CubeMap/front.jpg",
		"res/texture/CubeMap/back.jpg"
	};
	currentcamera->skybox = CreateRef<SkyBox>(texpaths);


	Test* current = nullptr;
	TestMenu* menu = new TestMenu(current);
	current = menu;
	menu->RegisterTest<TestClearColor>("Clear Color");
	menu->RegisterTest<TestTexture2D>("Texture2D");
	menu->RegisterTest<TestCube>("TestCube", 1080.0f, 960.0f);
	menu->RegisterTest<TestScene>("TestScene");
	menu->RegisterTest<NewTest>("NewTest");
	//TestClearColor test;
	/* Loop until the user closes the window */
	while (!glfwWindowShouldClose(window))
	{
		
		/* Render here */
		/*glClear(GL_COLOR_BUFFER_BIT);*/
		renderer.Clear();
		//test.OnUpdate(0.0f);
		//test.OnRender();


		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		if(currentcamera)
		GLCall(currentcamera->RenderSkyBox());
		currentcamera->GLPrecessInput(window, 0.05f);
		ImGui::ShowDemoWindow();
		
		if (current)
		{
			current->OnUpdate(0.0f);
			current->OnRender();
			ImGui::Begin("Test");
			if (current != menu && ImGui::Button("<-"))
			{
				delete current;
				current = menu;
			}
			current->OnImGuiRender();
			ImGui::End();
		}
		//test.OnImGuiRender();
		
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}

		/* Swap front and back buffers */
		glfwSwapBuffers(window);

		/* Poll for and process events */
		glfwPollEvents();
	}

	delete current;

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();


	glfwTerminate();
	return 0;
}

void mouse_callback(GLFWwindow* window, double Xpos, double Ypos)
{
	float xpos = static_cast<float>(Xpos);
	float ypos = static_cast<float>(Ypos);

	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos;

	//cout << "mouseX:" << xpos << "mouseY:" << ypos << endl;
	lastX = xpos;
	lastY = ypos;
	if(currentcamera&&glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS)
		currentcamera->GLMouseInput(xoffset, yoffset, true);
}
void mouse_scrollback(GLFWwindow* window, double xpos, double ypos)
{
	currentcamera->GLScrollInput(xpos, ypos);
}
