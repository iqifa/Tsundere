#include"glm/glm.hpp"
#include<iostream>

using namespace std;
using namespace glm;
#ifndef DEBUG

#define DEBUG
#define debuglog(...) log(__VA_ARGS__,__FILE__,__LINE__);
#define debugerror(...)error(__VA_ARGS__,__FILE__,__LINE__);

void log(const string str, string name, const char* file, int line);
void log(vec3& vec, const string name, const char* file, int line);
void log(unsigned int unit, const string name, const char* file, int line);
void log(const string str, const char* file, int line);
void log(float num, const char* file, int line);
void log(mat4 mat,string name, const char* file, int line);

void error(string msg, const char* file, int line);
#endif // !DUBUG

