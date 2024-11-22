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

void ShowDockSpace();

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
	Engine::ImGuiLayer iml;
	cout << glGetString(GL_VERSION) << endl;

	iml.OnAttach();

	glBlendFunc(GL_SRC0_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//glEnable(GL_CULL_FACE);
	glEnable(GL_STENCIL_TEST);
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetScrollCallback(window, mouse_scrollback); 


	Renderer renderer;
	FrameBufferSpecification specific;
	Ref<FrameBuffer>fb;
	fb = CreateRef<FrameBuffer>(specific);
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
	menu->RegisterTest<TestScene>("TestScene",true);
	glClearColor(0.5f, 0.5f, 1.0f, 1.0f);
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
		//if(currentcamera)
		//GLCall(currentcamera->RenderSkyBox());
		currentcamera->GLPrecessInput(window, 0.05f);
		ImGui::ShowDemoWindow();
		ShowDockSpace();
		//fb->Bind();
		if (current)
		{
			fb = current->getfb();
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
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		
		iml.End();

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

void ShowDockSpace()
{
	static bool opt_fullscreen = true;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;


	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	if (opt_fullscreen)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}
	else
	{
		dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
	}

	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", 0, window_flags);
	ImGui::PopStyleVar();
	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	// Submit the DockSpace
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}
	ImGui::End();

}