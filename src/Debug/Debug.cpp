#include "Debug.h"
void log(const std::string str, std::string name, const char* file, int line)
{
	std::cout << name << str;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}
void log(vec3& vec, const std::string name, const char* file, int line)
{
	std::cout << name << "x:" << vec[0] << "\t";
	std::cout << name << "y:" << vec[1] << "\t";
	std::cout << name << "z:" << vec[2] << "\t";
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}
void log(unsigned int unit, const std::string name, const char* file, int line)
{
	std::cout << name << ":" << unit;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}
void log(const std::string str, const char* file, int line)
{
	std::cout << str;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}

void log(float num, const char* file, int line)
{
	std::cout << num;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}

void log(mat4 mat,std::string name, const char* file, int line)
{
	std::cout << " " << mat[0].x << "\t" << mat[0].y << "\t" << mat[0].z << "\t" << mat[0].w << std::endl;
	std::cout << " " << mat[1].x << "\t" << mat[1].y << "\t" << mat[1].z << "\t" << mat[1].w << std::endl;
	std::cout << " " << mat[2].x << "\t" << mat[2].y << "\t" << mat[2].z << "\t" << mat[2].w << std::endl;
	std::cout << " " << mat[3].x << "\t" << mat[3].y << "\t" << mat[3].z << "\t" << mat[3].w << std::endl;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
}

void error(std::string msg, const char* file, int line)
{
	std::cout << msg;
	std::cout << "\tFile:" << file << "\tLine:" << line << std::endl;
	__debugbreak();
}
void warring(const std::string str, const char* file, int line)
{
	cout << "warring" << endl;
}