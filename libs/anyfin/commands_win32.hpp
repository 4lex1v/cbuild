
#define FIN_COMMANDS_HPP_IMPL

#include "anyfin/commands.hpp"
#include "anyfin/defer.hpp"

namespace Fin {

static Sys_Result<System_Command_Status> run_system_command (Memory_Arena &arena, String command_line) {
  SECURITY_ATTRIBUTES security { .nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE };
 
  HANDLE child_stdout_read, child_stdout_write;
  if (!CreatePipe(&child_stdout_read, &child_stdout_write, &security, 0))   return get_system_error();
  if (!SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, FALSE)) return get_system_error();
  defer { CloseHandle(child_stdout_read); };
  
  STARTUPINFO info {
    .cb = sizeof(STARTUPINFO),
    .dwFlags    = STARTF_USESTDHANDLES,
    .hStdOutput = child_stdout_write,
    .hStdError  = child_stdout_write,
  };

  PROCESS_INFORMATION process {};
  if (!CreateProcess(nullptr, const_cast<char *>(command_line.value), &security, &security, TRUE, 0, NULL, NULL, &info, &process))
    return get_system_error();
  defer {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  };

  CloseHandle(child_stdout_write);

  DWORD exit_code = 0;

  auto output_buffer = get_memory_at_current_offset<char>(arena);
  usize output_size  = 0;

  {
    while (true) {
      DWORD bytes_available = 0;
      if (!PeekNamedPipe(child_stdout_read, NULL, 0, NULL, &bytes_available, NULL)) {
        auto error_code = get_system_error_code();
        if (error_code != ERROR_BROKEN_PIPE) return get_system_error();
      }

      if (bytes_available == 0) {
        if (!GetExitCodeProcess(process.hProcess, &exit_code)) return get_system_error();
        if (exit_code != STILL_ACTIVE) break;
        continue;
      }

      /*
        Tiny hack to reserve space for the terminating 0
       */
      auto region = reserve<char>(arena, bytes_available);
      fin_ensure(region);

      DWORD bytes_read;
      if (!ReadFile(child_stdout_read, region, bytes_available, &bytes_read, NULL)) {
        auto error_code = get_system_error_code();
        /*
          According to ReadFile docs if the child process has closed its end of the pipe, indicated
          by the BROKEN_PIPE status, we can treat that as EOF.
         */
        if (error_code != ERROR_BROKEN_PIPE) return get_system_error();
      }

      fin_ensure(bytes_read == bytes_available);

      output_size += bytes_read;
    }
  }

  /*
    While the above loop should ensure that the process has finished and exited (since we got the exit_code),
    something is still off and making subsquent calls to dependent files may fail, because the child process
    didn't release all resources. For example, in the test kit calling delete_directory to cleanup the testsite
    without this wait block, not all resources are released and the delete call may fail.
   */
  WaitForSingleObject(process.hProcess, INFINITE);

  if (!output_size) return Ok(System_Command_Status { .status_code = static_cast<s32>(exit_code) });

  /*
    For some reason Windows includes CRLF at the end of the output, which is inc
   */
  if (output_size > 2 && ends_with(output_buffer, "\r\n")) output_size -= 2;
  output_buffer[output_size] = '\0';

  return Ok(System_Command_Status {
    .output      = String(output_buffer, output_size),
    .status_code = static_cast<s32>(exit_code),
  });
}

}
