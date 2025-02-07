#pragma once
#ifndef CAMERA
#define CAMERA

#include"ExternalFiles.h"
#include"HeadLine.h"
#include"Scene/SkyBox.h"

using namespace glm;

class SceneCamera {
public:
	SceneCamera();
	~SceneCamera();

	SceneCamera(vec3 Pos, vec3 Target,vec3 Up = vec3(0.0f, 1.0f, 0.0f),vec3 Front = vec3(0.0f, 0.0f, 1.0f));
	SceneCamera(vec3 Pos);
	mat4 LookAt(vec3 CameraPos, vec3 TargetPos, vec3 Up);
	mat4 GetViewTarget();
	mat4 GetViewFront();
	mat4 GetProj();

	void Setx(float x);
	void Sety(float y);
	void Setz(float z);
	void SetPos(vec3 Pos);
	void SetTarget(vec3 Target);


	float getx();
	float gety();
	float getz();
	vec3 getpos();
	vec3 getTarget();
	vec3 getFront();

	void GLPrecessInput(GLFWwindow* window,float speed);
	void GLMouseInput(float xoffset, float yoffset, GLboolean constrainPitch);
	void GLScrollInput(float xoffset, float yoffset);
	
	void RenderSkyBox();
private:
	vec3 cameraPos;
	vec3 cameraTarget;
	vec3 cameraFront;
	vec3 cameraUp;
	float yaw=90.0f, pitch=0.0f, roll = 0.0f,fov=1.0f;
	float sensitive=0.1f;
	
public:
	Ref<SkyBox>skybox;
};
extern SceneCamera* currentcamera;
#endif // !CAMERA