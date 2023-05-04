#include "Debug.h"
void log(const string str, string name, const char* file, int line)
{
	cout << name << str;
	cout << "\tFile:" << file << "\tLine:" << line << endl;
}
void log(vec3& vec, const string name, const char* file, int line)
{
	cout << name << "x:" << vec[0] << "\t";
	cout << name << "y:" << vec[1] << "\t";
	cout << name << "z:" << vec[2] << "\t";
	cout << "\tFile:" << file << "\tLine:" << line << endl;
}
void log(unsigned int unit, const string name, const char* file, int line)
{
	cout << name << ":" << unit;
	cout << "\tFile:" << file << "\tLine:" << line << endl;
}
void log(const string str, const char* file, int line)
{
	cout << str;
	cout << "\tFile:" << file << "\tLine:" << line << endl;
}

void log(float num, const char* file, int line)
{
	cout << num;
	cout << "\tFile:" << file << "\tLine:" << line << endl;
}

void log(mat4 mat,string name, const char* file, int line)
{
	cout << " " << mat[0].x << "\t" << mat[0].y << "\t" << mat[0].z << "\t" << mat[0].w << endl;
	cout << " " << mat[1].x << "\t" << mat[1].y << "\t" << mat[1].z << "\t" << mat[1].w << endl;
	cout << " " << mat[2].x << "\t" << mat[2].y << "\t" << mat[2].z << "\t" << mat[2].w << endl;
	cout << " " << mat[3].x << "\t" << mat[3].y << "\t" << mat[3].z << "\t" << mat[3].w << endl;
	cout << "\tFile:" << file << "\tLine:" << line << endl;
}

void error(string msg, const char* file, int line)
{
	cout << msg;
	cout << "\tFile:" << file << "\tLine:" << line << endl;
	__debugbreak();
}
