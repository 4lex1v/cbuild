
#define FIN_CONSOLE_HPP_IMPL

#include "anyfin/console.hpp"
#include "anyfin/win32.hpp"

namespace Fin {

static Sys_Result<void> write_to_stdout (String message) {
  auto stdout = GetStdHandle(STD_OUTPUT_HANDLE);

  DWORD bytes_written = 0;
  while (true) {
    auto bytes_to_write = message.length - bytes_written;

    auto cursor = message.value + bytes_written;
    if (!WriteFile(stdout, cursor, bytes_to_write, &bytes_written, nullptr))
      return get_system_error();

    if (bytes_written == message.length) break;
  }

  /*
    Not checking the error for this one, cause according to the docs it would return an error
    for attempting to flush a console output, which is what this function may be used for most
    often. This may be helpful when we pipe std out, though.
   */
  FlushFileBuffers(stdout);

#ifdef DEV_BUILD
  OutputDebugString(message);
#endif

  return Ok();
}

}
