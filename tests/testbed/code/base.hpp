
#pragma once

// Export macros for GCC and Clang
#if defined(__GNUC__) || defined(__clang__)
  #define EXPORT_SYMBOL __attribute__((visibility("default")))
// Export macro for MSVC (Visual Studio)
#elif _MSC_VER
  #define EXPORT_SYMBOL __declspec(dllexport)
#else
  #define EXPORT_SYMBOL
#endif

