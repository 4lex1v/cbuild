
#pragma once

#include "anyfin/base.hpp"

namespace Fin {

[[noreturn]] static void terminate (u32 exit_code);

};

#ifndef FIN_PROCESS_HPP_IMPL
  #ifdef PLATFORM_WIN32
    #include "anyfin/process_win32.hpp"
  #else
    #error "Unsupported platform"
  #endif
#endif
