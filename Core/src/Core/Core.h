#ifdef T_PLATFORM_WINDOWS
	#ifdef T_BUILD_DLL
		#define T_API _declspec(dllexport)
	#else
		#define T_API _declspec(dllimport)
	#endif // T_BUILD_DLL

#else 
#error Only Support Window!!! 


#endif