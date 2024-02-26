#pragma once
#ifndef HEAD
#define HEAD
#include<iostream>
#include<fstream>
#include<sstream>
#include<string>
#include<unordered_map>
#include<vector>

using namespace std;
template<typename T>
using Ref = shared_ptr<T>;
template<typename T, typename ... Args>
Ref<T>CreateRef(Args &&...args)
{
	return make_shared<T>(std::forward<Args>(args)...);
}

template<typename T>
using Ptr = unique_ptr<T>;
template<typename T, typename ... Args>
Ptr<T>CreatePtr(Args &&...args)
{
	return make_unique<T>(std::forward<Args>(args)...);
}

#endif // !HEAD