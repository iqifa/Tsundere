#pragma once
#ifndef CAMERA
#define CAMERA

#include "Platform/RHI/RHICommandBuffer.h"
#include"HeadLine.h"
#include"Scene/SkyBox.h"

using namespace glm;

class T_API SceneCamera {
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
	void SetAspect(float w, float h) { m_AspectRatio = w / h; }

	float getx();
	float gety();
	float getz();
	vec3 getpos();
	vec3 getTarget();
	vec3 getFront();

	void GLPrecessInput(GLFWwindow* window,float speed);
	void GLMouseInput(float xoffset, float yoffset, GLboolean constrainPitch);
	void GLScrollInput(float xoffset, float yoffset);
	float m_AspectRatio = 1.125f;
	void RenderSkyBox(RHICommandBuffer& commandBuffer);
private:
	vec3 cameraPos;
	vec3 cameraTarget;
	vec3 cameraFront;
	vec3 cameraUp;
	float yaw=90.0f, pitch=0.0f, roll = 0.0f,fov=45.0f;
	float sensitive=0.1f;

	// Previous frame's rotation-only proj*view for the skybox, so SkyBox.shader
	// can write motion vectors. Sky pixels with zero velocity make TAA reuse
	// history from the old camera orientation, which shows up as smearing.
	mat4 m_PrevSkyViewProj = mat4(1.0f);
	bool m_PrevSkyValid = false;

public:
	Ref<SkyBox>skybox;
};
extern T_API SceneCamera* currentcamera;
#endif // !CAMERA