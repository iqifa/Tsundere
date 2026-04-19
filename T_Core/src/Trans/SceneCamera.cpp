#include"SceneCamera.h"


const vec3 WorldUp = vec3(0.0f,1.0f,0.0f);


SceneCamera* currentcamera = new SceneCamera(vec3(0.0f, 0.0f, -3.0f));

SceneCamera::SceneCamera()
{

}

SceneCamera::~SceneCamera()
{
}

SceneCamera::SceneCamera(vec3 Pos, vec3 Target, vec3 Up , vec3 Front ) :cameraPos(Pos), cameraTarget(Target), cameraFront(Front), cameraUp(Up)
{

}

SceneCamera::SceneCamera(vec3 Pos) : cameraPos(Pos), cameraFront(vec3(0.0f, 0.0f, 1.0f)), cameraTarget(cameraPos + cameraFront),cameraUp(vec3(0.0f, 1.0f, 0.0f))
{
	
	/*vector<string> texpaths{
		"res/texture/CubeMap/right.jpg",
		"res/texture/CubeMap/left.jpg",
		"res/texture/CubeMap/top.jpg",
		"res/texture/CubeMap/bottom.jpg",
		"res/texture/CubeMap/front.jpg",
		"res/texture/CubeMap/back.jpg"
	};
	skybox= CreateRef<Component::SkyBox>(texpaths);*/
}

mat4 SceneCamera::LookAt(vec3 CameraPos, vec3 TargetPos, vec3 CameraUp = vec3(0.0f, 1.0f, 0.0f))
{
	vec cameraDirection = normalize(TargetPos - CameraPos);
	vec3 cameraRight = normalize(cross(cameraDirection, CameraUp));
	vec3 Up = cross(cameraRight, cameraDirection);
	mat4 Result(1.0f);
	Result[0][0] = cameraRight.x;
	Result[1][0] = cameraRight.y;
	Result[2][0] = cameraRight.z;
	Result[0][1] = Up.x;
	Result[1][1] = Up.y;
	Result[2][1] = Up.z;
	Result[0][2] = -cameraDirection.x;
	Result[1][2] = -cameraDirection.y;
	Result[2][2] = -cameraDirection.z;
	Result[3][0] = -dot(cameraRight, CameraPos);
	Result[3][1] = -dot(Up, CameraPos);
	Result[3][2] = dot(cameraDirection, CameraPos);
	return Result;
}

mat4 SceneCamera::GetViewTarget()
{
	return LookAt(cameraPos, cameraTarget, vec3(0.0f, 1.0f, 0.0f));
}

mat4 SceneCamera::GetViewFront()
{

	return LookAt(cameraPos, cameraPos + cameraFront, WorldUp);
}

mat4 SceneCamera::GetProj()
{
	mat4 proj_fru=glm::frustum(-3.0f, 3.0f, 3.0f, 3.0f, 0.1f, 100.0f);
	mat4 proj_psp = glm::perspective(radians(fov), 1080.0f / 960.0f, 0.1f, 100.0f);
	return proj_psp;
}

void SceneCamera::GLPrecessInput(GLFWwindow* window, float speed)
{
	vec3 cameraUp(0.0f, 1.0f, 0.0f);
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		cameraPos += cameraFront * speed;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		cameraPos -= normalize(cross(cameraFront, cameraUp)) * speed;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		cameraPos -= cameraFront * speed;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		cameraPos += normalize(cross(cameraFront, cameraUp)) * speed;
}

void SceneCamera::GLMouseInput(float xoffset, float yoffset, GLboolean constrainPitch)
{
	xoffset *= sensitive;
	yoffset *= sensitive;

	yaw += xoffset;
	pitch += yoffset;

	if (constrainPitch)
	{
		if (pitch > 89.0f)
			pitch = 89.0f;
		if (pitch < -89.0f)
			pitch = -89.0f;
	}

	
	//debuglog((unsigned int)this, "cameraID");

	/// <summary>
	/// 问题 ，做完以后记得回来看
	/// </summary>
	/// <param name="xoffset"></param>
	/// <param name="yoffset"></param>
	/// <param name="constrainPitch"></param>
	vec3 front;
	front.x = cos(radians(yaw)) * cos(radians(pitch));
	front.y = sin(radians(pitch));
	front.z = sin(radians(yaw)) * cos(radians(pitch));

	/*front.x = sin(radians(yaw));
	front.y = sin(radians(pitch));
	front.z = cos(radians(yaw));*/

	cameraFront = normalize(front);
	vec3 Right = normalize(cross(cameraFront, WorldUp));
	cameraUp = normalize(cross(Right, cameraFront));
}
void SceneCamera::GLScrollInput(float xoffset, float yoffset)
{
	if (fov >= 1.0f && fov <= 45.0f)
		fov -= yoffset;
	if (fov <= 1.0f)
		fov = 1.0f;
	if (fov >= 45.0f)
		fov = 45.0f;
}
void SceneCamera::RenderSkyBox()
{
	glDepthMask(GL_FALSE);
	skybox->m_Shader->Bind();

	mat4 model(1.0f);
	mat4 view = GetViewFront();
	view = mat4(mat3(view));
	mat4 proj = GetProj();
	mat4 mvp = proj * view * model;
	skybox->m_Shader->SetUniformMat4f("proj", proj);
	skybox->m_Shader->SetUniformMat4f("view", view);

	Renderer renderer;
	/*glBindTexture(GL_TEXTURE_CUBE_MAP, skybox->m_Cmp->GetMap());*/
	skybox->Bind();
	renderer.DrawArray(*skybox->m_vao, *skybox->m_Shader);

	skybox->m_Shader->UnBind();
	glDepthMask(GL_TRUE);
}
void SceneCamera::Setx(float x)
{
	cameraPos.x = x;
}

void SceneCamera::Sety(float y)
{
	cameraPos.y = y;
}

void SceneCamera::Setz(float z)
{
	cameraPos.z = z;
}

void SceneCamera::SetPos(vec3 Pos)
{
	cameraPos = Pos;
}

void SceneCamera::SetTarget(vec3 Target)
{
	cameraTarget = Target;
}

float SceneCamera::getx()
{
	return cameraPos.x;
}

float SceneCamera::gety()
{
	return cameraPos.y;
}

float SceneCamera::getz()
{
	return cameraPos.z;
}

vec3 SceneCamera::getpos()
{
	return cameraPos;
}

vec3 SceneCamera::getTarget()
{
	return cameraTarget;
}
vec3 SceneCamera::getFront()
{
	return  cameraFront;
}