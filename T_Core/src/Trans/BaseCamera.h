#include<ExternalFiles.h>
class BaseCamera {
public:
	BaseCamera() = default;
	~BaseCamera() = default;

	void Setx(float x);
	void Sety(float y);
	void Setz(float z);
	void SetPos(glm::vec3 Pos);


	float getx();
	float gety();
	float getz();
	glm::vec3 getpos();
	glm::vec3 getFront();
private:
	glm::vec3 cameraPos;
	glm::vec3 cameraFront;
	glm::vec3 cameraUp;

	glm::mat4 m_viewMatrix;


	float yaw = 90.0f, pitch = 0.0f, roll = 0.0f, fov = 1.0f;
	float sensitive = 0.1f;
};