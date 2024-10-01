
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/platform.hpp"

namespace Fin {

struct System_Command_Status {
  String output;
  s32    status_code;
};

static Sys_Result<System_Command_Status> run_system_command (Memory_Arena &arena, String command_line);

}

#ifndef FIN_COMMANDS_HPP_IMPL
  #ifdef PLATFORM_WIN32
    #include "anyfin/commands_win32.hpp"
  #else
    #error "Unsupported platform"
  #endif
#endif
