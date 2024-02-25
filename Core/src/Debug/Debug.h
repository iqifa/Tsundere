
#include"glm/glm.hpp"
#include<iostream>

using namespace glm;
#ifndef DEBUG

#define DEBUG
#define debuglog(...) log(__VA_ARGS__,__FILE__,__LINE__);
#define debugerror(...)error(__VA_ARGS__,__FILE__,__LINE__);
#define debugwarring(...)warring(__VA_ARGS__,__FILE__,__LINE__);

void log(const std::string str, std::string name, const char* file, int line);
void log(vec3& vec, const std::string name, const char* file, int line);
void log(unsigned int unit, const std::string name, const char* file, int line);
void log(const std::string str, const char* file, int line);
void log(float num, const char* file, int line);
void log(mat4 mat,std::string name, const char* file, int line);

void error(std::string msg, const char* file, int line);

void warring(const std::string str, const char* file, int line);
#endif // !DUBUG

