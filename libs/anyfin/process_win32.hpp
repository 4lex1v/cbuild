
#define FIN_PROCESS_HPP_IMPL

#include "anyfin/process.hpp"
#include "anyfin/win32.hpp"

namespace Fin {

[[noreturn]] static void terminate (u32 exit_code) {
  ExitProcess(exit_code);
  __builtin_unreachable();
}

};
