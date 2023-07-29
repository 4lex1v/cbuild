
#pragma once

#ifndef DLL_API
#ifdef _WIN32
#define DLL_API __declspec(dllexport)
#else
#define DLL_API __attribute__((visibility("default")))
#endif
#endif

DLL_API void test_phrase(); 
