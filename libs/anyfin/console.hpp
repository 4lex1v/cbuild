
#pragma once

#include "anyfin/strings.hpp"
#include "anyfin/platform.hpp"

namespace Fin {

static Sys_Result<void> write_to_stdout (String message);

}

#ifndef FIN_CONSOLE_HPP_IMPL
  #ifdef PLATFORM_WIN32
    #include "anyfin/console_win32.hpp"
  #else
    #error "Unsupported platform"
  #endif
#endif
