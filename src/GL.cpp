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
	ImGuiLayer iml;
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
	menu->RegisterTest<TestScene>("TestScene");
	menu->RegisterTest<NewTest>("NewTest");
	//TestClearColor test;
	/* Loop until the user closes the window */
	while (!glfwWindowShouldClose(window))
	{
		
		/* Render here */
		/*glClear(GL_COLOR_BUFFER_BIT);*/
		fb->Bind();
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
			unsigned int textureid = fb->GetClolorAttachmentRenderID();
			ImGui::Image((void*)textureid, ImVec2{ 64.0f,64.0f });
			current->OnImGuiRender();
			ImGui::End();
		}
		//test.OnImGuiRender();
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		fb->UnBind();
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
