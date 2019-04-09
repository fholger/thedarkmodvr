/*
MiniZip DLL import/export
Dmitriy Vetutnev, ODANT 2017
*/

#ifndef MINIZIP_EXTERN_H
#define MINIZIP_EXTERN_H

#if defined(_WIN32) && defined(MINIZIP_DLL)
	#if defined(MINIZIP_BUILDING)
		#define MINIZIP_EXTERN __declspec(dllexport)
	#else
		#define MINIZIP_EXTERN __declspec(dllimport)
	#endif
#endif

#ifndef MINIZIP_EXTERN
    #define MINIZIP_EXTERN extern
#endif

#endif /* MINIZIP_EXTERN_H */
