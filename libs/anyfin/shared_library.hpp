
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/file_system.hpp"
#include "anyfin/platform.hpp"

namespace Fin {

struct Shared_Library;

static Sys_Result<Shared_Library *> load_shared_library (const File_Path &library_file_path);

static Sys_Result<void> unload_library (Shared_Library &library);

template <typename T>
static Sys_Result<T *> lookup_symbol (const Shared_Library &library, const String &symbol_name);

}

#ifndef FIN_SHARED_LIBRARY_HPP_IMPL
  #ifdef PLATFORM_WIN32
    #include "shared_library_win32.hpp"
  #else
    #error "Unsupported platform"
  #endif
#endif
