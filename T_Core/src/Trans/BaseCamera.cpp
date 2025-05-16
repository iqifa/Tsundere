#include"BaseCamera.h"
using namespace glm;
void BaseCamera::Setx(float x)
{
	cameraPos.x = x;
}

void BaseCamera::Sety(float y)
{
	cameraPos.y = y;
}

void BaseCamera::Setz(float z)
{
	cameraPos.z = z;
}

void BaseCamera::SetPos(vec3 Pos)
{
	cameraPos = Pos;
}

float BaseCamera::getx()
{
	return cameraPos.x;
}

float BaseCamera::gety()
{
	return cameraPos.y;
}

float BaseCamera::getz()
{
	return cameraPos.z;
}

vec3 BaseCamera::getpos()
{
	return cameraPos;
}

vec3 BaseCamera::getFront()
{
	return  cameraFront;
}