#include"GLDebug.h"
using namespace std;

void GLClearError()
{
	while (glGetError() != GL_NO_ERROR);
}

bool GLLogCall(const char* function, const char* file, int line)
{
	while (GLenum error = glGetError())
	{
		cout << "[Opengl Error]" << "(" << error << ")" << ":" << function << " " << file << ":" << line << endl;
		return false;
	}
	return true;
}
