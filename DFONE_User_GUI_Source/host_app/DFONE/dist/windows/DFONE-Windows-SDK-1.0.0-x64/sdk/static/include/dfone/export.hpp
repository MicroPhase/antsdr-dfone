#pragma once

#if defined(_WIN32) && !defined(DFONE_STATIC)
#if defined(DFONE_BUILDING_LIBRARY)
#define DFONE_API __declspec(dllexport)
#else
#define DFONE_API __declspec(dllimport)
#endif
#else
#define DFONE_API
#endif
