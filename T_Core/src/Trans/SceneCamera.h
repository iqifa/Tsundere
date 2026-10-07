#pragma once
#ifndef CAMERA
#define CAMERA

#include "Platform/RHI/RHICommandBuffer.h"
#include"HeadLine.h"
#include"Scene/SkyBox.h"


class T_API SceneCamera {
public:
	SceneCamera();
	~SceneCamera();

	SceneCamera(glm::vec3 Pos, glm::vec3 Target,glm::vec3 Up = glm::vec3(0.0f, 1.0f, 0.0f),glm::vec3 Front = glm::vec3(0.0f, 0.0f, 1.0f));
	SceneCamera(glm::vec3 Pos);
	glm::mat4 LookAt(glm::vec3 CameraPos, glm::vec3 TargetPos, glm::vec3 Up);
	glm::mat4 GetViewTarget();
	glm::mat4 GetViewFront();
	glm::mat4 GetProj();

	void Setx(float x);
	void Sety(float y);
	void Setz(float z);
	void SetPos(glm::vec3 Pos);
	void SetTarget(glm::vec3 Target);
	void SetAspect(float w, float h) { m_AspectRatio = w / h; }

	float getx();
	float gety();
	float getz();
	glm::vec3 getpos();
	glm::vec3 getTarget();
	glm::vec3 getFront();
	float getFov() { return fov; }
	float getFar() { return far_plane; }
	float getNear() { return near_plan; }

	void ProcessKeyboard(GLFWwindow* window, float speed);
	void ProcessMouseLook(float dx, float dy, bool constrainPitch = true);
	void ProcessScroll(float delta);
	float m_AspectRatio = 1.125f;
	void RenderSkyBox(RHICommandBuffer& commandBuffer);
private:
	glm::vec3 cameraPos;
	glm::vec3 cameraTarget;
	glm::vec3 cameraFront;
	glm::vec3 cameraUp;
	float yaw=90.0f, pitch=0.0f, roll = 0.0f,fov=45.0f;
	float far_plane = 1000.0f, near_plan = 1.0f;
	float sensitive=0.1f;

	// Previous frame's rotation-only proj*view for the skybox, so SkyBox.shader
	// can write motion vectors. Sky pixels with zero velocity make TAA reuse
	// history from the old camera orientation, which shows up as smearing.
	glm::mat4 m_PrevSkyViewProj = glm::mat4(1.0f);
	bool m_PrevSkyValid = false;

public:
	Ref<SkyBox>skybox;
};
extern T_API SceneCamera* currentcamera;
#endif // !CAMERA